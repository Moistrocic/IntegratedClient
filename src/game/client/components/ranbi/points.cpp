/* ranbi: ported from RanbiClient 1.0.2
 * clientsSource/RanbiClient/src/game/client/components/ranbi/points.cpp
 * Adapted to the IntegratedClient HTTP API (IHttpRequest/shared_ptr, HttpGet returns
 * a unique_ptr) and to the current CGameClient client list. */
#include "points.h"

#include <game/client/gameclient.h>

#include <cctype>
#include <cstdio>
#include <set>

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
	std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
	std::vector<std::string> DoneRequests;
	std::vector<std::string> FailRequests;
	for(const auto &PointsRequest : m_vPointsRequests)
	{
		std::string Name = PointsRequest.first;
		EHttpState State = PointsRequest.second->State();
		if(State == EHttpState::DONE)
		{
			Run(Name, PointsRequest.second);
			DoneRequests.push_back(Name);
		}
		else if(State == EHttpState::ERROR || State == EHttpState::ABORTED)
		{
			FailRequests.push_back(Name);
		}
	}
	Lock.unlock();
	for(const auto &Name : DoneRequests)
		FinishRequest(Name);
	for(const auto &Name : FailRequests)
	{
		FinishRequest(Name);
		RemovePoints(Name);
	}

	std::unique_lock LockCache(m_PointsMutex);
	std::set<std::string> NameSet;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameClient()->m_aClients[i].m_Active && GameClient()->m_aClients[i].m_aName[0] != '\0')
			NameSet.insert(GameClient()->m_aClients[i].m_aName);
	}
	for(auto It = m_PointsCache.begin(); It != m_PointsCache.end();)
	{
		if(!NameSet.contains(It->first))
			It = m_PointsCache.erase(It);
		else
			++It;
	}
}

void CPoints::AddRequest(const std::string &Name, const std::shared_ptr<IHttpRequest> &Request)
{
	std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
	m_vPointsRequests.emplace_back(Name, Request);
}

void CPoints::FinishRequest(const std::string &Name)
{
	std::unique_lock<std::mutex> Lock(m_PointsRequestsMutex);
	std::erase_if(m_vPointsRequests, [&Name](const auto &Item) {
		return Item.first == Name;
	});
}

int CPoints::GetPoints(const std::string &Name)
{
	std::unique_lock LockPoints(m_PointsMutex);
	if(!m_PointsCache.contains(Name))
	{
		m_PointsCache.insert_or_assign(Name, -1);
		std::string NameUrl = EncodePlayerName(Name);
		const std::string Url = "https://ddnet.org/players/?json2=" + NameUrl;
		std::shared_ptr<IHttpRequest> PointsTask = HttpGet(Url.c_str());
		if(!PointsTask)
			return 0;
		PointsTask->Timeout(CTimeout{10000, 0, 500, 10});
		Http()->Run(PointsTask);
		AddRequest(Name, PointsTask);
	}
	return m_PointsCache.at(Name);
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

void CPoints::ParseJson(const json_value *pObj, const std::string &TargetName)
{
	if(!pObj || pObj->type != json_object)
	{
		m_PointsCache.insert_or_assign(TargetName, 0);
		return;
	}

	const json_value *PlayerName = json_object_get(pObj, "player");
	const json_value *PlayerPointsObject = json_object_get(pObj, "points");
	const json_value *PlayerPoints = json_object_get(PlayerPointsObject, "points");

	if(!PlayerName || PlayerName->type == json_none || !PlayerPoints || PlayerPoints->type == json_none)
	{
		m_PointsCache.insert_or_assign(TargetName, 0);
		return;
	}

	const char *Name = json_string_get(PlayerName);
	int Points = json_int_get(PlayerPoints);
	if(Name && TargetName == Name)
		m_PointsCache.insert_or_assign(TargetName, Points);
}

void CPoints::Run(const std::string &Name, std::shared_ptr<IHttpRequest> pPointsRequest)
{
	std::shared_ptr<IHttpRequest> pRequest = nullptr;
	std::swap(pPointsRequest, pRequest);
	json_value *pObj = pRequest->ResultJson();
	ParseJson(pObj, Name);
	json_value_free(pObj);
}
