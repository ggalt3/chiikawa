#include "PixelFinder.h"
#include "../EnginePrediction/EnginePrediction.h"
#include <cmath>

void CPixelFinder::DrawVerticalLine(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::PixelFinder::Enabled.Value || !pLocal || !pLocal->IsAlive())
		return;

	// Check if the line drawing key is held
	if (GetAsyncKeyState(Vars::Misc::PixelFinder::DrawLineKey.Value) & 0x8000)
	{
		if (!m_bFirstPointSet)
		{
			// Set first point (trace to wall)
			Vec3 vEyePos = pLocal->GetEyePosition();
			Vec3 vViewAngles = { pCmd->viewangles.x, pCmd->viewangles.y, 0.f };
			Vec3 vForward;
			Math::AngleVectors(vViewAngles, &vForward);
			Vec3 vEndPos = vEyePos + vForward * 8192.f;

			CGameTrace trace = {};
			Ray_t ray;
			ray.Init(vEyePos, vEndPos);
			CTraceFilterWorldAndPropsOnly filter;
			filter.pSkip = pLocal;
			I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);

			if (trace.DidHit() && !trace.endpos.IsZero())
			{
				m_vLineStart = trace.endpos;
				m_vWallNormal = trace.plane.normal;
				m_bIsDisplacement = trace.IsDispSurface();
				m_bFirstPointSet = true;
			}
		}
		else
		{
			// Set second point (vertical line end)
			Vec3 vEyePos = pLocal->GetEyePosition();
			Vec3 vViewAngles = { pCmd->viewangles.x, pCmd->viewangles.y, 0.f };
			Vec3 vForward;
			Math::AngleVectors(vViewAngles, &vForward);
			Vec3 vEndPos = vEyePos + vForward * 8192.f;

			CGameTrace trace = {};
			Ray_t ray;
			ray.Init(vEyePos, vEndPos);
			CTraceFilterWorldAndPropsOnly filter;
			filter.pSkip = pLocal;
			I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);

			if (trace.DidHit() && !trace.endpos.IsZero())
			{
				// Keep the X and Y from the first point, use Z from the second
				m_vLineEnd = { m_vLineStart.x, m_vLineStart.y, trace.endpos.z };
				m_bDrawingLine = true;
			}
		}
	}
	else
	{
		if (m_bDrawingLine)
		{
			// Detect pixelsurf points when line is complete
			DetectPixelSurfPoints(pLocal, pCmd);
			m_bDrawingLine = false;
		}
		// Only clear first point when starting new line, keep line for visualization
		m_bFirstPointSet = false;
	}
}

void CPixelFinder::DetectPixelSurfPoints(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::PixelFinder::Enabled.Value || !pLocal || !pLocal->IsAlive())
		return;

	// Validate line coordinates
	if (m_vLineStart.IsZero() || m_vLineEnd.IsZero())
		return;

	m_bClearingPoints = true;

	// Calculate the line direction and length
	float flLineLength = fabs(m_vLineEnd.z - m_vLineStart.z);
	if (flLineLength < 1.f)
	{
		m_bClearingPoints = false;
		return;
	}

	m_vDetectedPoints.clear();

	// Get TF2 gravity and tickrate
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	if (!sv_gravity)
	{
		m_bClearingPoints = false;
		return;
	}

	float flGravity = sv_gravity->GetFloat();
	float flTickRate = 1.0f / I::GlobalVars->interval_per_tick;
	float flTargetZVel = -((flGravity / 2.0f) / flTickRate);

	// Offset value based on surface type (DNA approach)
	float flTestAl = 15.97803f;
	if (m_bIsDisplacement)
		flTestAl = 16.001f;

	// Store original player state
	Vec3 vOriginalOrigin = pLocal->m_vecOrigin();
	Vec3 vOriginalVelocity = pLocal->m_vecVelocity();
	int nOriginalFlags = pLocal->m_fFlags();
	int nOriginalButtons = pCmd->buttons;
	float flOriginalForwardMove = pCmd->forwardmove;
	float flOriginalSideMove = pCmd->sidemove;

	// Scan along the vertical line (DNA approach - unit intervals)
	std::vector<PixelSurfPoint_t> vNewPoints;
	int iMaxSteps = static_cast<int>(flLineLength);
	vNewPoints.reserve(iMaxSteps + 1);

	for (int i = 0; i <= iMaxSteps; i++)
	{
		float flLerp = static_cast<float>(i);
		float flHeight;
		
		if (m_vLineStart.z > m_vLineEnd.z)
			flHeight = m_vLineStart.z - flLerp;
		else
			flHeight = m_vLineEnd.z - flLerp;

		// Calculate player position offset from wall based on wall normal (DNA geometric approach)
		Vec3 vPlayerPos = m_vLineStart;
		
		if (m_vWallNormal.x < 0 && m_vWallNormal.y < 0.f)
			vPlayerPos = { m_vLineStart.x - flTestAl, m_vLineStart.y - flTestAl, flHeight };
		else if (m_vWallNormal.x < 0 && m_vWallNormal.y > 0.f)
			vPlayerPos = { m_vLineStart.x - flTestAl, m_vLineStart.y + flTestAl, flHeight };
		else if (m_vWallNormal.x > 0 && m_vWallNormal.y < 0.f)
			vPlayerPos = { m_vLineStart.x + flTestAl, m_vLineStart.y - flTestAl, flHeight };
		else if (m_vWallNormal.x > 0 && m_vWallNormal.y > 0.f)
			vPlayerPos = { m_vLineStart.x + flTestAl, m_vLineStart.y + flTestAl, flHeight };
		else if (m_vWallNormal.x == 0.f && m_vWallNormal.y > 0.f)
			vPlayerPos = { m_vLineStart.x, m_vLineStart.y + flTestAl, flHeight };
		else if (m_vWallNormal.x == 0.f && m_vWallNormal.y < 0.f)
			vPlayerPos = { m_vLineStart.x, m_vLineStart.y - flTestAl, flHeight };
		else if (m_vWallNormal.x < 0 && m_vWallNormal.y == 0.f)
			vPlayerPos = { m_vLineStart.x - flTestAl, m_vLineStart.y, flHeight };
		else if (m_vWallNormal.x > 0 && m_vWallNormal.y == 0.f)
			vPlayerPos = { m_vLineStart.x + flTestAl, m_vLineStart.y, flHeight };

		// Check if position is valid (between floor and ceiling) - geometric check
		CGameTrace traceDown = {};
		Ray_t rayDown;
		rayDown.Init({ vPlayerPos.x, vPlayerPos.y, vPlayerPos.z + 54.f }, { vPlayerPos.x, vPlayerPos.y, vPlayerPos.z - 1000.f });
		CTraceFilterWorldAndPropsOnly filterDown;
		filterDown.pSkip = pLocal;
		I::EngineTrace->TraceRay(rayDown, MASK_SOLID, &filterDown, &traceDown);

		CGameTrace traceUp = {};
		Ray_t rayUp;
		rayUp.Init({ vPlayerPos.x, vPlayerPos.y, vPlayerPos.z + 54.f }, { vPlayerPos.x, vPlayerPos.y, vPlayerPos.z + 1000.f });
		CTraceFilterWorldAndPropsOnly filterUp;
		filterUp.pSkip = pLocal;
		I::EngineTrace->TraceRay(rayUp, MASK_SOLID, &filterUp, &traceUp);

		// Skip if not between floor and ceiling (geometric constraint)
		if (vPlayerPos.z + 1.f < traceDown.endpos.z || vPlayerPos.z + 1.f > traceUp.endpos.z)
			continue;

		// Set player to test position
		pLocal->m_vecOrigin() = vPlayerPos;
		pLocal->m_vecVelocity() = { 0.f, 0.f, 0.f };
		pLocal->m_fFlags() &= ~FL_ONGROUND;

		// Set movement input to align with wall (DNA approach)
		Vec3 vWallAngles = { -m_vWallNormal.x, -m_vWallNormal.y, 0.f };
		Vec3 vToWall;
		Math::VectorAngles(vWallAngles, vToWall);
		float flRotation = Math::Deg2Rad(vToWall.y - pCmd->viewangles.y);
		float flCosRot = cos(flRotation);
		float flSinRot = sin(flRotation);
		pCmd->forwardmove = flCosRot * 10.f;
		pCmd->sidemove = -flSinRot * 10.f;
		pCmd->buttons |= IN_JUMP;
		pCmd->buttons |= IN_DUCK;

		// Run prediction
		F::EnginePrediction.Start(pLocal, pCmd);
		F::EnginePrediction.End(pLocal, pCmd);

		// Check if Z velocity matches pixelsurf velocity
		Vec3 vPredictedVelocity = pLocal->m_vecVelocity();
		bool bIsPixelSurfVel = (fabs(vPredictedVelocity.z - flTargetZVel) < 0.5f);

		if (bIsPixelSurfVel)
		{
			// Calculate the actual pixelsurf point (offset back from wall)
			Vec3 vPixelSurfPoint = vPlayerPos;
			
			if (m_vWallNormal.x < 0 && m_vWallNormal.y < 0.f)
				vPixelSurfPoint = { vPlayerPos.x + flTestAl, vPlayerPos.y + flTestAl, vPlayerPos.z };
			else if (m_vWallNormal.x < 0 && m_vWallNormal.y > 0.f)
				vPixelSurfPoint = { vPlayerPos.x + flTestAl, vPlayerPos.y - flTestAl, vPlayerPos.z };
			else if (m_vWallNormal.x > 0 && m_vWallNormal.y < 0.f)
				vPixelSurfPoint = { vPlayerPos.x - flTestAl, vPlayerPos.y + flTestAl, vPlayerPos.z };
			else if (m_vWallNormal.x > 0 && m_vWallNormal.y > 0.f)
				vPixelSurfPoint = { vPlayerPos.x - flTestAl, vPlayerPos.y - flTestAl, vPlayerPos.z };
			else if (m_vWallNormal.x == 0.f && m_vWallNormal.y > 0.f)
				vPixelSurfPoint = { vPlayerPos.x, vPlayerPos.y - flTestAl, vPlayerPos.z };
			else if (m_vWallNormal.x == 0.f && m_vWallNormal.y < 0.f)
				vPixelSurfPoint = { vPlayerPos.x, vPlayerPos.y + flTestAl, vPlayerPos.z };
			else if (m_vWallNormal.x < 0 && m_vWallNormal.y == 0.f)
				vPixelSurfPoint = { vPlayerPos.x + flTestAl, vPlayerPos.y, vPlayerPos.z };
			else if (m_vWallNormal.x > 0 && m_vWallNormal.y == 0.f)
				vPixelSurfPoint = { vPlayerPos.x - flTestAl, vPlayerPos.y, vPlayerPos.z };

			PixelSurfPoint_t point;
			point.m_vPosition = vPixelSurfPoint;
			point.m_sMap = I::EngineClient->GetLevelName();
			point.m_flAnimationProgress = 0.f;
			point.m_bIsAppearing = true;
			point.m_bIsRemoving = false;
			point.m_flCurrentSize = 0.f;
			vNewPoints.push_back(point);
		}
	}

	// Restore original state
	pLocal->m_vecOrigin() = vOriginalOrigin;
	pLocal->m_vecVelocity() = vOriginalVelocity;
	pLocal->m_fFlags() = nOriginalFlags;
	pCmd->buttons = nOriginalButtons;
	pCmd->forwardmove = flOriginalForwardMove;
	pCmd->sidemove = flOriginalSideMove;

	// Swap vectors to minimize race condition window
	m_vDetectedPoints.swap(vNewPoints);
	m_bClearingPoints = false;
}

bool CPixelFinder::TestPixelSurfAtPosition(CTFPlayer* pLocal, const Vec3& vPosition, const Vec3& vWallNormal, CUserCmd* pCmd)
{
	// Store original state
	Vec3 vOriginalOrigin = pLocal->m_vecOrigin();
	Vec3 vOriginalVelocity = pLocal->m_vecVelocity();
	int nOriginalFlags = pLocal->m_fFlags();

	// Set player to test position
	pLocal->m_vecOrigin() = vPosition;
	pLocal->m_vecVelocity() = { 0.f, 0.f, 0.f };
	pLocal->m_fFlags() &= ~FL_ONGROUND;

	// Calculate TF2 pixelsurf velocity based on gravity and tickrate
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	if (!sv_gravity)
	{
		pLocal->m_vecOrigin() = vOriginalOrigin;
		pLocal->m_vecVelocity() = vOriginalVelocity;
		pLocal->m_fFlags() = nOriginalFlags;
		return false;
	}

	float flGravity = sv_gravity->GetFloat();
	float flTickRate = 1.0f / I::GlobalVars->interval_per_tick;
	float flTargetZVel = -((flGravity / 2.0f) / flTickRate);

	// Run prediction to simulate falling (like arbuzebra does)
	CUserCmd testCmd = *pCmd;
	testCmd.forwardmove = 0.f;
	testCmd.sidemove = 0.f;
	testCmd.upmove = 0.f;
	testCmd.buttons = 0;

	F::EnginePrediction.Start(pLocal, &testCmd);
	F::EnginePrediction.End(pLocal, &testCmd);

	// Check if Z velocity matches calculated pixelsurf velocity
	Vec3 vPredictedVelocity = pLocal->m_vecVelocity();
	bool bIsPixelSurfVel = (fabs(vPredictedVelocity.z - flTargetZVel) < 0.5f);

	// Restore original state
	pLocal->m_vecOrigin() = vOriginalOrigin;
	pLocal->m_vecVelocity() = vOriginalVelocity;
	pLocal->m_fFlags() = nOriginalFlags;

	return bIsPixelSurfVel;
}

void CPixelFinder::RenderPoints()
{
	if (!Vars::Misc::PixelFinder::Enabled.Value)
		return;

	// Thread safety: Don't render if points are being modified
	if (m_bClearingPoints)
		return;

	float flDeltaTime = I::GlobalVars->frametime;
	const float flAppearanceDuration = 0.5f;
	const float flDisappearanceDuration = 0.5f;
	const float flSizeAnimationSpeed = 5.0f;
	const float flNormalSize = 9.f;
	const float flInCrosshairSize = 12.f;

	// Create a copy of points to avoid race conditions
	std::vector<PixelSurfPoint_t> vPointsCopy;
	{
		vPointsCopy = m_vDetectedPoints;
	}

	// Update animations on the copy
	for (auto& point : vPointsCopy)
	{
		if (point.m_bIsAppearing)
		{
			point.m_flAnimationProgress += flDeltaTime / flAppearanceDuration;
			if (point.m_flAnimationProgress >= 1.f)
			{
				point.m_flAnimationProgress = 1.f;
				point.m_bIsAppearing = false;
			}
		}
		else if (point.m_bIsRemoving)
		{
			point.m_flAnimationProgress -= flDeltaTime / flDisappearanceDuration;
			if (point.m_flAnimationProgress <= 0.f)
			{
				point.m_flAnimationProgress = 0.f;
			}
		}

		// Check if in crosshair
		Vec3 vScreenPos;
		point.m_bInCrosshair = false;
		if (SDK::W2S(point.m_vPosition, vScreenPos))
		{
			point.m_bInCrosshair = InCrosshair(Vec2(vScreenPos.x, vScreenPos.y), flNormalSize);
		}

		// Calculate target size
		float flTargetSize;
		if (point.m_bIsRemoving)
		{
			flTargetSize = 0.f;
		}
		else
		{
			flTargetSize = point.m_bInCrosshair ? flInCrosshairSize : flNormalSize;
			if (point.m_bIsAppearing)
			{
				flTargetSize *= point.m_flAnimationProgress;
			}
		}

		// Animate size
		point.m_flCurrentSize += (flTargetSize - point.m_flCurrentSize) * (1.f - exp(-flSizeAnimationSpeed * flDeltaTime));
	}

	// Draw points from the copy
	for (const auto& point : vPointsCopy)
	{
		Vec3 vScreenPos;
		if (SDK::W2S(point.m_vPosition, vScreenPos))
		{
			// Validate screen coordinates
			if (!isfinite(vScreenPos.x) || !isfinite(vScreenPos.y))
				continue;
			if (vScreenPos.x < 0 || vScreenPos.x > H::Draw.m_nScreenW ||
				vScreenPos.y < 0 || vScreenPos.y > H::Draw.m_nScreenH)
				continue;

			float flAlpha = point.m_flAnimationProgress * 255.f;
			Color_t tColor = Vars::Misc::PixelFinder::PointColor.Value;
			tColor.a = static_cast<byte>(flAlpha);

			if (tColor.a > 0)
			{
				// Draw circle
				H::Draw.LineCircle(vScreenPos.x, vScreenPos.y, point.m_flCurrentSize, 32, tColor);

				// Draw filled circle if in crosshair
				if (point.m_bInCrosshair)
				{
					Color_t tFillColor = tColor;
					tFillColor.a = static_cast<byte>(flAlpha * 0.3f);
					if (tFillColor.a > 0)
					{
						H::Draw.FillCircle(vScreenPos.x, vScreenPos.y, point.m_flCurrentSize, 32, tFillColor);
					}
				}
			}
		}
	}
}

void CPixelFinder::RenderSavedPoints()
{
	if (!Vars::Misc::PixelFinder::Enabled.Value)
		return;

	// Thread safety: Don't render if points are being modified
	if (m_bClearingPoints)
		return;

	// Create a copy to avoid race conditions
	std::vector<PixelSurfPoint_t> vSavedCopy;
	{
		vSavedCopy = m_vSavedPoints;
	}

	for (const auto& point : vSavedCopy)
	{
		Vec3 vScreenPos;
		if (SDK::W2S(point.m_vPosition, vScreenPos))
		{
			// Validate screen coordinates
			if (!isfinite(vScreenPos.x) || !isfinite(vScreenPos.y))
				continue;
			if (vScreenPos.x < 0 || vScreenPos.x > H::Draw.m_nScreenW ||
				vScreenPos.y < 0 || vScreenPos.y > H::Draw.m_nScreenH)
				continue;

			Color_t tColor = Vars::Misc::PixelFinder::SavedPointColor.Value;
			
			if (tColor.a > 0)
			{
				// Draw larger circle for saved points
				H::Draw.LineCircle(vScreenPos.x, vScreenPos.y, 12.f, 32, tColor);
				H::Draw.FillCircle(vScreenPos.x, vScreenPos.y, 12.f, 32, { tColor.r, tColor.g, tColor.b, 50 });
			}
		}
	}
}

void CPixelFinder::Render()
{
	if (!Vars::Misc::PixelFinder::Enabled.Value)
		return;

	// Create local copies of coordinates to avoid race conditions
	Vec3 vLineStart = m_vLineStart;
	Vec3 vLineEnd = m_vLineEnd;

	// Draw the line - always draw if coordinates are valid
	if (!vLineStart.IsZero() && !vLineEnd.IsZero())
	{
		Vec3 vStartScreen, vEndScreen;
		if (SDK::W2S(vLineStart, vStartScreen) && 
			SDK::W2S(vLineEnd, vEndScreen))
		{
			// Validate screen coordinates
			if (!isfinite(vStartScreen.x) || !isfinite(vStartScreen.y) ||
				!isfinite(vEndScreen.x) || !isfinite(vEndScreen.y))
				return;
			if (vStartScreen.x < 0 || vStartScreen.x > H::Draw.m_nScreenW ||
				vStartScreen.y < 0 || vStartScreen.y > H::Draw.m_nScreenH)
				return;
			if (vEndScreen.x < 0 || vEndScreen.x > H::Draw.m_nScreenW ||
				vEndScreen.y < 0 || vEndScreen.y > H::Draw.m_nScreenH)
				return;

			Color_t tLineColor = Vars::Misc::PixelFinder::LineColor.Value;
			if (tLineColor.a > 0)
			{
				H::Draw.Line(vStartScreen.x, vStartScreen.y, vEndScreen.x, vEndScreen.y, tLineColor);
			}
		}
	}

	// Enable point rendering
	RenderPoints();
	RenderSavedPoints();
}

bool CPixelFinder::InCrosshair(const Vec2& vScreenPos, float flRadius)
{
	int iScreenX, iScreenY;
	I::EngineClient->GetScreenSize(iScreenX, iScreenY);

	float flCenterX = iScreenX / 2.0f;
	float flCenterY = iScreenY / 2.0f;
	float flDX = flCenterX - vScreenPos.x;
	float flDY = flCenterY - vScreenPos.y;

	return (flDX * flDX + flDY * flDY) <= (flRadius * flRadius);
}

Vec3 CPixelFinder::RoundPosition(const Vec3& vPoint)
{
	const float flEpsilon = 0.001f;
	const float flZOffset = vPoint.z < 0.0f ? -0.97f : 0.031f;
	return Vec3(vPoint.x, vPoint.y, floor(vPoint.z + flEpsilon) + flZOffset);
}

void CPixelFinder::SavePoint()
{
	if (!Vars::Misc::PixelFinder::Enabled.Value)
		return;

	// Thread safety: Don't modify while rendering
	if (m_bClearingPoints)
		return;

	// Create a copy to avoid race conditions
	std::vector<PixelSurfPoint_t> vDetectedCopy;
	{
		vDetectedCopy = m_vDetectedPoints;
	}

	// Find the point in crosshair and save it
	for (const auto& point : vDetectedCopy)
	{
		if (point.m_bInCrosshair)
		{
			// Check if point already exists
			bool bExists = false;
			std::vector<PixelSurfPoint_t> vSavedCopy;
			{
				vSavedCopy = m_vSavedPoints;
			}
			
			for (const auto& saved : vSavedCopy)
			{
				if (saved.m_vPosition.DistTo(point.m_vPosition) < 1.0f)
				{
					bExists = true;
					break;
				}
			}

			if (!bExists)
			{
				PixelSurfPoint_t newPoint = point;
				newPoint.m_bIsAppearing = false;
				newPoint.m_flAnimationProgress = 1.f;
				
				m_bClearingPoints = true;
				m_vSavedPoints.push_back(newPoint);
				m_vDetectedPoints.clear();
				m_bClearingPoints = false;
				return;
			}
		}
	}
}

void CPixelFinder::ClearPoints()
{
	m_vDetectedPoints.clear();
}

void CPixelFinder::ClearSavedPoints()
{
	m_bClearingPoints = true;
	m_vSavedPoints.clear();
	m_bClearingPoints = false;
}

void CPixelFinder::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::PixelFinder::Enabled.Value || !pLocal || !pLocal->IsAlive())
		return;

	DrawVerticalLine(pLocal, pCmd);

	// Check for save key
	if (GetAsyncKeyState(Vars::Misc::PixelFinder::SavePointKey.Value) & 0x8000)
	{
		SavePoint();
	}

	// Check for clear key
	if (GetAsyncKeyState(Vars::Misc::PixelFinder::ClearPointsKey.Value) & 0x8000)
	{
		ClearPoints();
	}
}
