/* ranbi: ported from RanbiClient 1.0.2
 * clientsSource/RanbiClient/src/game/client/components/ranbi/points.cpp
 * Adapted to the IntegratedClient HTTP API (IHttpRequest/shared_ptr, HttpGet returns
 * a unique_ptr) and to the current CGameClient client list.
 * task-50: de-duplicate in-flight requests, keep failed names in a 30 s backoff
 * (one retry max), skip pending/backoff entries while cleaning, and fail a
 * name-mismatch JSON instead of leaving it pending forever. */
#include "points.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <set>

namespace
{
	constexpr int64_t POINTS_FAILURE_BACKOFF_MS = 30 * 1000;
	constexpr int64_t POINTS_GIVE_UP_MS = 24 * 60 * 60 * 1000;
	// task-84: fetched/failed cache entries outlive the player. Residual chat lines/scoreboard/
	// nameplates keep asking for a name for ~20 s after it leaves; deleting the cache entry then
	// caused a request -> fetch -> cleanup -> request loop. Keep untouched entries for a long
	// stale window and enforce a large LRU cap as the memory bound.
	constexpr int64_t POINTS_CACHE_STALE_MS = 30 * 60 * 1000;
	constexpr size_t POINTS_CACHE_MAX_ENTRIES = 4096;

	void LogPoints(const char *pFormat, ...)
	{
		if(!g_Config.m_Debug)
			return;
		char aBody[256];
		va_list Args;
		va_start(Args, pFormat);
		str_format_v(aBody, sizeof(aBody), pFormat, Args);
		va_end(Args);
		log_info("ranbi-points", "%s", aBody);
	}
} // namespace

void CPoints::OnReset()
{
	std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
	for(auto &Request : m_vPointsRequests)
	{
		if(Request.second)
			Request.second->Abort();
	}
	m_vPointsRequests.clear();
	Lock.unlock();
	ClearPoints();
}

void CPoints::OnRender()
{
	std::vector<std::pair<std::string, std::shared_ptr<IHttpRequest>>> vDone;
	std::vector<std::string> vFailed;
	{
		std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
		for(const auto &PointsRequest : m_vPointsRequests)
		{
			if(!PointsRequest.second)
			{
				vFailed.push_back(PointsRequest.first);
				continue;
			}
			const EHttpState State = PointsRequest.second->State();
			if(State == EHttpState::DONE)
				vDone.emplace_back(PointsRequest.first, PointsRequest.second);
			else if(State == EHttpState::ERROR || State == EHttpState::ABORTED)
				vFailed.push_back(PointsRequest.first);
		}
	}

	for(const auto &Done : vDone)
	{
		const bool Ok = Run(Done.first, Done.second);
		FinishRequest(Done.first);
		if(!Ok)
			MarkFailure(Done.first);
	}
	for(const auto &Name : vFailed)
	{
		FinishRequest(Name);
		MarkFailure(Name);
	}

	// task-84: keep fetched (incl. 0 points), pending and failure/backoff entries after the
	// player leaves: residual chat lines/scoreboard/nameplates still reference the name for a
	// while, and an immediately erased fetched entry caused a request->fetch->erase loop
	// (in-flight de-dupe only prevented simultaneous duplicates). Only purge entries that are
	// not active, not in-flight and untouched for the long stale window; enforce a large LRU cap
	// as the memory bound so recent/displayed names are never affected.
	std::unique_lock LockCache(m_PointsMutex);
	std::set<std::string> NameSet;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameClient()->m_aClients[i].m_Active && GameClient()->m_aClients[i].m_aName[0] != '\0')
			NameSet.insert(GameClient()->m_aClients[i].m_aName);
	}
	const int64_t Now = time_get();
	const int64_t StaleTicks = POINTS_CACHE_STALE_MS * time_freq() / 1000;
	for(auto It = m_PointsCache.begin(); It != m_PointsCache.end();)
	{
		if(NameSet.contains(It->first))
		{
			// Active players stay cached; refresh the access time so a player leaving does not
			// start its stale window from an old access.
			It->second.m_LastAccess = Now;
			++It;
			continue;
		}
		if(It->second.m_Points == -1)
		{
			// in-flight request: the OnRender request completion handling owns this entry
			It->second.m_LastAccess = Now;
			++It;
			continue;
		}
		const int64_t Age = Now - It->second.m_LastAccess;
		if(Age < StaleTicks)
		{
			++It;
			continue;
		}
		LogPoints("cleanup erase name=%s reason=stale age_ms=%lld fetched=%d failures=%d requests=%d hits=%d",
			It->first.c_str(), (long long)(Age * 1000 / time_freq()), It->second.m_Fetched ? 1 : 0,
			It->second.m_FailureCount, It->second.m_RequestStartCount, It->second.m_CacheHitCount);
		It = m_PointsCache.erase(It);
	}

	if(m_PointsCache.size() > POINTS_CACHE_MAX_ENTRIES)
	{
		// Large LRU bound: evict the least-recently-used non-active, non-in-flight entries.
		std::vector<std::unordered_map<std::string, SCacheEntry>::iterator> vEvict;
		vEvict.reserve(m_PointsCache.size());
		for(auto It = m_PointsCache.begin(); It != m_PointsCache.end(); ++It)
		{
			if(NameSet.contains(It->first) || It->second.m_Points == -1)
				continue;
			vEvict.push_back(It);
		}
		std::sort(vEvict.begin(), vEvict.end(), [](const auto &A, const auto &B) {
			return A->second.m_LastAccess < B->second.m_LastAccess;
		});
		size_t NumToEvict = m_PointsCache.size() - POINTS_CACHE_MAX_ENTRIES;
		for(const auto &It : vEvict)
		{
			if(NumToEvict == 0)
				break;
			LogPoints("cleanup erase name=%s reason=lru age_ms=%lld fetched=%d requests=%d",
				It->first.c_str(), (long long)((Now - It->second.m_LastAccess) * 1000 / time_freq()),
				It->second.m_Fetched ? 1 : 0, It->second.m_RequestStartCount);
			m_PointsCache.erase(It);
			NumToEvict--;
		}
	}
}

void CPoints::AddRequest(const std::string &Name, const std::shared_ptr<IHttpRequest> &Request)
{
	std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
	for(const auto &Existing : m_vPointsRequests)
	{
		if(Existing.first == Name)
		{
			LogPoints("dedupe add cid/name=%s", Name.c_str());
			if(Request)
				Request->Abort();
			return;
		}
	}
	m_vPointsRequests.emplace_back(Name, Request);
}

void CPoints::FinishRequest(const std::string &Name)
{
	std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
	std::erase_if(m_vPointsRequests, [&Name](const auto &Item) {
		return Item.first == Name;
	});
}

bool CPoints::HasPendingRequest(const std::string &Name) const
{
	std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
	for(const auto &Request : m_vPointsRequests)
	{
		if(Request.first == Name)
			return true;
	}
	return false;
}

int CPoints::GetPoints(const std::string &Name)
{
	const int64_t Now = time_get();

	{
		std::unique_lock LockPoints(m_PointsMutex);
		auto It = m_PointsCache.find(Name);
		if(It != m_PointsCache.end())
		{
			It->second.m_LastAccess = Now;
			if(It->second.m_Points == -1)
			{
				// request marked pending; de-dupe below
			}
			else if(It->second.m_Fetched)
			{
				if(It->second.m_CacheHitCount++ == 0)
					LogPoints("cache hit name=%s points=%d hits=%d", Name.c_str(), It->second.m_Points, It->second.m_CacheHitCount);
				return It->second.m_Points;
			}
			else if(It->second.m_FailureCount >= 2)
			{
				if(It->second.m_CacheHitCount++ == 0)
					LogPoints("cache hit name=%s points=%d give_up=1 hits=%d", Name.c_str(), It->second.m_Points, It->second.m_CacheHitCount);
				return 0; // gave up after the one allowed retry
			}
			else if(It->second.m_RetryAt > Now)
			{
				LogPoints("backoff name=%s retry_in_ms=%lld", Name.c_str(), (long long)(It->second.m_RetryAt - Now));
				return 0;
			}
		}
	}

	// task-50: an in-flight request for the same name is never started twice, even if
	// the cache entry was removed/marked differently by another display path.
	if(HasPendingRequest(Name))
	{
		LogPoints("dedupe pending name=%s", Name.c_str());
		std::unique_lock LockPoints(m_PointsMutex);
		auto &Entry = m_PointsCache[Name];
		Entry.m_Points = -1;
		Entry.m_LastAccess = Now;
		return -1;
	}

	{
		std::unique_lock LockPoints(m_PointsMutex);
		auto &Entry = m_PointsCache[Name];
		Entry.m_Points = -1;
		Entry.m_Fetched = false;
		Entry.m_RetryAt = 0;
		Entry.m_LastAccess = Now;
		Entry.m_RequestStartCount++;
		LogPoints("request start name=%s count=%d", Name.c_str(), Entry.m_RequestStartCount);
	}

	std::string NameUrl = EncodePlayerName(Name);
	const std::string Url = "https://ddnet.org/players/?json2=" + NameUrl;
	std::shared_ptr<IHttpRequest> PointsTask = HttpGet(Url.c_str());
	if(!PointsTask)
	{
		LogPoints("request create failed name=%s", Name.c_str());
		MarkFailure(Name);
		return 0;
	}
	PointsTask->Timeout(CTimeout{10000, 0, 500, 10});
	Http()->Run(PointsTask);
	AddRequest(Name, PointsTask);
	return -1;
}

bool CPoints::TryGetPoints(const char *pName, int *pPoints) const
{
	if(!pName || !pName[0] || !pPoints)
		return false;
	std::unique_lock LockPoints(m_PointsMutex);
	auto It = m_PointsCache.find(pName);
	if(It == m_PointsCache.end() || !It->second.m_Fetched)
		return false;
	// task-84: a cache hit keeps the entry alive for the stale/LRU cleanup.
	It->second.m_LastAccess = time_get();
	It->second.m_CacheHitCount++;
	*pPoints = It->second.m_Points;
	return true;
}

void CPoints::RemovePoints(const std::string &Name)
{
	std::unique_lock LockPoints(m_PointsMutex);
	m_PointsCache.erase(Name);
}

void CPoints::ClearPoints()
{
	std::unique_lock LockPoints(m_PointsMutex);
	m_PointsCache.clear();
}

void CPoints::MarkFailure(const std::string &Name)
{
	const int64_t Now = time_get();
	std::unique_lock LockPoints(m_PointsMutex);
	auto &Entry = m_PointsCache[Name];
	Entry.m_Points = 0;
	Entry.m_Fetched = false;
	Entry.m_LastAccess = Now;
	Entry.m_FailureCount++;
	if(Entry.m_FailureCount < 2)
	{
		Entry.m_RetryAt = Now + POINTS_FAILURE_BACKOFF_MS * time_freq() / 1000;
		LogPoints("failure name=%s backoff_ms=%lld retry_allowed=1", Name.c_str(), (long long)POINTS_FAILURE_BACKOFF_MS);
	}
	else
	{
		Entry.m_RetryAt = Now + POINTS_GIVE_UP_MS * time_freq() / 1000;
		LogPoints("failure name=%s give_up=1", Name.c_str());
	}
}

std::string CPoints::EncodePlayerName(const std::string &Name)
{
	std::string Encoded;
	Encoded.reserve(Name.length() * 3);
	for(unsigned char c : Name)
	{
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			Encoded += c;
		}
		else
		{
			char aBuf[4];
			snprintf(aBuf, sizeof(aBuf), "%%%02X", c);
			Encoded += aBuf;
		}
	}
	return Encoded;
}

bool CPoints::ParseJson(const json_value *pObj, const std::string &TargetName, int *pPoints)
{
	if(!pObj || pObj->type != json_object)
	{
		LogPoints("parse fail name=%s reason=not_object", TargetName.c_str());
		return false;
	}

	const json_value *PlayerName = json_object_get(pObj, "player");
	if(PlayerName == nullptr || PlayerName->type == json_none)
	{
		// task-52: ddnet.org returns {} for a player without any finished maps.
		// That is a valid "0 points" result, not a failed request.
		LogPoints("parse empty -> points=0 name=%s", TargetName.c_str());
		*pPoints = 0;
		return true;
	}
	if(PlayerName->type != json_string)
	{
		LogPoints("parse fail name=%s reason=invalid_player", TargetName.c_str());
		return false;
	}

	const json_value *PlayerPointsObject = json_object_get(pObj, "points");
	if(!PlayerPointsObject || PlayerPointsObject->type != json_object)
	{
		LogPoints("parse empty -> points=0 name=%s", TargetName.c_str());
		*pPoints = 0;
		return true;
	}

	const json_value *PlayerPoints = json_object_get(PlayerPointsObject, "points");
	if(!PlayerPoints || PlayerPoints->type != json_integer)
	{
		// No score data for this player (new player / no finished maps): 0 points success.
		LogPoints("parse empty -> points=0 name=%s", TargetName.c_str());
		*pPoints = 0;
		return true;
	}

	const char *pName = json_string_get(PlayerName);
	if(!pName || TargetName != pName)
	{
		LogPoints("parse fail name=%s reason=name_mismatch(returned=%s)", TargetName.c_str(), pName ? pName : "null");
		return false;
	}

	*pPoints = json_int_get(PlayerPoints);
	return true;
}

bool CPoints::Run(const std::string &Name, std::shared_ptr<IHttpRequest> pPointsRequest)
{
	if(!pPointsRequest)
		return false;
	json_value *pObj = pPointsRequest->ResultJson();
	int Points = 0;
	const bool Ok = ParseJson(pObj, Name, &Points);
	if(pObj)
		json_value_free(pObj);
	if(!Ok)
		return false;

	std::unique_lock LockPoints(m_PointsMutex);
	auto &Entry = m_PointsCache[Name];
	Entry.m_Points = Points;
	Entry.m_Fetched = true;
	Entry.m_RetryAt = 0;
	Entry.m_FailureCount = 0;
	Entry.m_LastAccess = time_get();
	LogPoints("request done name=%s points=%d requests=%d", Name.c_str(), Points, Entry.m_RequestStartCount);
	return true;
}
