#ifndef GAME_CLIENT_COMPONENTS_RANBI_POINTS_H
#define GAME_CLIENT_COMPONENTS_RANBI_POINTS_H

/* ranbi: ported from RanbiClient 1.0.2
 * clientsSource/RanbiClient/src/game/client/components/ranbi/points.h
 * Adapted to the IntegratedClient (DDNet 20.1.1) HTTP API: IHttpRequest/shared_ptr,
 * CClient name list instead of the old snapshot helper.
 * task-50: request de-duplication/failure backoff are an IntegratedClient fix. */

#include <engine/http.h>
#include <engine/shared/json.h>

#include <game/client/component.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CPoints : public CComponent
{
	struct SCacheEntry
	{
		int m_Points = 0; // points when m_Fetched, 0 otherwise
		bool m_Fetched = false; // true = request completed successfully (0 is a valid value)
		int64_t m_RetryAt = 0; // monotonic time before which a failed name is not retried
		int m_FailureCount = 0; // 0 success, 1 first failure (one retry allowed), 2+ give up
	};

	std::vector<std::pair<std::string, std::shared_ptr<IHttpRequest>>> m_vPointsRequests;
	std::unordered_map<std::string, SCacheEntry> m_PointsCache;
	mutable std::mutex m_PointsRequestsMutex;
	mutable std::mutex m_PointsMutex;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void ClearPoints();

	void AddRequest(const std::string &Name, const std::shared_ptr<IHttpRequest> &Request);
	void FinishRequest(const std::string &Name);

	int GetPoints(const std::string &Name);
	void RemovePoints(const std::string &Name);
	// task-55: only true when a request completed successfully (0 is a valid value).
	bool TryGetPoints(const char *pName, int *pPoints) const;

private:
	std::string EncodePlayerName(const std::string &Name);
	bool ParseJson(const json_value *pObj, const std::string &TargetName, int *pPoints);
	bool Run(const std::string &Name, std::shared_ptr<IHttpRequest> pPointsRequest);
	bool HasPendingRequest(const std::string &Name) const;
	void MarkFailure(const std::string &Name);
};

#endif
