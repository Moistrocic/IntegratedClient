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

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <set>

namespace
{
	constexpr int64_t POINTS_FAILURE_BACKOFF_MS = 30 * 1000;
	constexpr int64_t POINTS_GIVE_UP_MS = 24 * 60 * 60 * 1000;

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

	// cleanup inactive names, but never erase in-flight or freshly failed entries.
	std::unique_lock LockCache(m_PointsMutex);
	std::set<std::string> NameSet;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameClient()->m_aClients[i].m_Active && GameClient()->m_aClients[i].m_aName[0] != '\0')
			NameSet.insert(GameClient()->m_aClients[i].m_aName);
	}
	const int64_t Now = time_get();
	for(auto It = m_PointsCache.begin(); It != m_PointsCache.end();)
	{
		if(NameSet.contains(It->first) || It->second.m_Points == -1 || It->second.m_RetryAt > Now)
		{
			++It;
			continue;
		}
		It = m_PointsCache.erase(It);
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
		const auto It = m_PointsCache.find(Name);
		if(It != m_PointsCache.end())
		{
			if(It->second.m_Points == -1)
			{
				// request marked pending; de-dupe below
			}
			else if(It->second.m_Fetched)
			{
				return It->second.m_Points;
			}
			else if(It->second.m_FailureCount >= 2)
			{
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
		return -1;
	}

	{
		std::unique_lock LockPoints(m_PointsMutex);
		auto &Entry = m_PointsCache[Name];
		Entry.m_Points = -1;
		Entry.m_Fetched = false;
		Entry.m_RetryAt = 0;
	}
	LogPoints("request start name=%s", Name.c_str());

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
	const json_value *PlayerPointsObject = json_object_get(pObj, "points");
	const json_value *PlayerPoints = PlayerPointsObject ? json_object_get(PlayerPointsObject, "points") : nullptr;

	if(!PlayerName || PlayerName->type != json_string || !PlayerPoints || PlayerPoints->type != json_integer)
	{
		LogPoints("parse fail name=%s reason=missing_fields", TargetName.c_str());
		return false;
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
	LogPoints("request done name=%s points=%d", Name.c_str(), Points);
	return true;
}
