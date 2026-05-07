#pragma once
#include "../../SDK/SDK.h"

struct PixelSurfPoint_t
{
	Vec3 m_vPosition = {};
	std::string m_sMap = "";
	float m_flAnimationProgress = 0.f;
	bool m_bIsAppearing = true;
	bool m_bIsRemoving = false;
	bool m_bFirstPointSet = false;
	bool m_bDrawingLine = false;
	bool m_bIsDisplacement = false;
	float m_flCurrentSize = 0.f;
	bool m_bInCrosshair = false;
};

class CPixelFinder
{
private:
	Vec3 m_vLineStart = {};
	Vec3 m_vLineEnd = {};
	Vec3 m_vWallNormal = {};
	bool m_bDrawingLine = false;
	bool m_bFirstPointSet = false;
	bool m_bIsDisplacement = false;
	bool m_bClearingPoints = false;
	std::vector<PixelSurfPoint_t> m_vDetectedPoints;
	std::vector<PixelSurfPoint_t> m_vSavedPoints;
	
	void DrawVerticalLine(CTFPlayer* pLocal, CUserCmd* pCmd);
	void DetectPixelSurfPoints(CTFPlayer* pLocal, CUserCmd* pCmd);
	void RenderPoints();
	void RenderSavedPoints();
	bool InCrosshair(const Vec2& vScreenPos, float flRadius);
	Vec3 RoundPosition(const Vec3& vPoint);
	bool TestPixelSurfAtPosition(CTFPlayer* pLocal, const Vec3& vPosition, const Vec3& vWallNormal, CUserCmd* pCmd);
	
	// Pixelsurf Assist
	void RunPixelSurfAssist(CTFPlayer* pLocal, CUserCmd* pCmd);
	PixelSurfPoint_t* FindNearestSavedPoint(CTFPlayer* pLocal);
	
public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
	void Render();
	void SavePoint();
	void ClearPoints();
	void ClearSavedPoints();
	
	const std::vector<PixelSurfPoint_t>& GetDetectedPoints() const { return m_vDetectedPoints; }
	const std::vector<PixelSurfPoint_t>& GetSavedPoints() const { return m_vSavedPoints; }
};

ADD_FEATURE(CPixelFinder, PixelFinder);
