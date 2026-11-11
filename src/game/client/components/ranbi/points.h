#ifndef GAME_CLIENT_COMPONENTS_RANBI_POINTS_H
#define GAME_CLIENT_COMPONENTS_RANBI_POINTS_H

/* ranbi: ported from RanbiClient 1.0.2
 * clientsSource/RanbiClient/src/game/client/components/ranbi/points.h
 * Adapted to the IntegratedClient (DDNet 20.1.1) HTTP API: IHttpRequest/shared_ptr,
 * CClient name list instead of the old snapshot helper. */

#include <engine/http.h>
#include <engine/shared/json.h>

#include <game/client/component.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CPoints : public CComponent
{
	std::vector<std::pair<std::string, std::shared_ptr<IHttpRequest>>> m_vPointsRequests;
	std::unordered_map<std::string, int> m_PointsCache;
	std::mutex m_PointsRequestsMutex;
	std::mutex m_PointsMutex;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void ClearPoints();

	void AddRequest(const std::string &Name, const std::shared_ptr<IHttpRequest> &Request);
	void FinishRequest(const std::string &Name);

	int GetPoints(const std::string &Name);
	void RemovePoints(const std::string &Name);

private:
	std::string EncodePlayerName(const std::string &Name);
	void ParseJson(const json_value *pObj, const std::string &TargetName);
	void Run(const std::string &Name, std::shared_ptr<IHttpRequest> pPointsRequest);
};

#endif
