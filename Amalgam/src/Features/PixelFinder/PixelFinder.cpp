#include "PixelFinder.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"
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

	// Scan along the vertical line height, but use actual wall surface at each height
	std::vector<PixelSurfPoint_t> vNewPoints;
	int iMaxVerticalSteps = static_cast<int>(flLineLength);
	vNewPoints.reserve(iMaxVerticalSteps + 1);

	int iPositionsTested = 0;
	int iPositionsPassedGeometry = 0;
	int iPositionsPassedVelocity = 0;

	for (int j = 0; j <= iMaxVerticalSteps; j++)
	{
		float flVerticalLerp = static_cast<float>(j);

		// Calculate height (vertical)
		float flHeight;
		if (m_vLineStart.z > m_vLineEnd.z)
			flHeight = m_vLineStart.z - flVerticalLerp;
		else
			flHeight = m_vLineEnd.z - flVerticalLerp;

		// Trace from line start at this height to find actual wall surface
		Vec3 vTraceStart = { m_vLineStart.x, m_vLineStart.y, flHeight };
		Vec3 vTraceEnd = vTraceStart - m_vWallNormal * 100.f;

		CGameTrace traceToWall = {};
		Ray_t rayToWall;
		rayToWall.Init(vTraceStart, vTraceEnd);
		CTraceFilterWorldAndPropsOnly filterToWall;
		filterToWall.pSkip = pLocal;
		I::EngineTrace->TraceRay(rayToWall, MASK_SOLID, &filterToWall, &traceToWall);

		if (!traceToWall.DidHit() || traceToWall.endpos.IsZero())
			continue;

		Vec3 vWallPos = traceToWall.endpos;

		// Calculate player position offset from wall based on wall normal (DNA geometric approach)
		Vec3 vPlayerPos;
		
		if (m_vWallNormal.x < 0 && m_vWallNormal.y < 0.f)
			vPlayerPos = { vWallPos.x - flTestAl, vWallPos.y - flTestAl, flHeight };
		else if (m_vWallNormal.x < 0 && m_vWallNormal.y > 0.f)
			vPlayerPos = { vWallPos.x - flTestAl, vWallPos.y + flTestAl, flHeight };
		else if (m_vWallNormal.x > 0 && m_vWallNormal.y < 0.f)
			vPlayerPos = { vWallPos.x + flTestAl, vWallPos.y - flTestAl, flHeight };
		else if (m_vWallNormal.x > 0 && m_vWallNormal.y > 0.f)
			vPlayerPos = { vWallPos.x + flTestAl, vWallPos.y + flTestAl, flHeight };
		else if (m_vWallNormal.x == 0.f && m_vWallNormal.y > 0.f)
			vPlayerPos = { vWallPos.x, vWallPos.y + flTestAl, flHeight };
		else if (m_vWallNormal.x == 0.f && m_vWallNormal.y < 0.f)
			vPlayerPos = { vWallPos.x, vWallPos.y - flTestAl, flHeight };
		else if (m_vWallNormal.x < 0 && m_vWallNormal.y == 0.f)
			vPlayerPos = { vWallPos.x - flTestAl, vWallPos.y, flHeight };
		else if (m_vWallNormal.x > 0 && m_vWallNormal.y == 0.f)
			vPlayerPos = { vWallPos.x + flTestAl, vWallPos.y, flHeight };

		iPositionsTested++;

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

		iPositionsPassedGeometry++;

		// Set player to test position
		pLocal->m_vecOrigin() = vPlayerPos;
		pLocal->m_vecVelocity() = { 0.f, 0.f, 0.f };
		pLocal->m_fFlags() &= ~FL_ONGROUND;

		// Set view angles to point into wall (disconnect from actual player)
		Vec3 vWallAngles = { -m_vWallNormal.x, -m_vWallNormal.y, 0.f };
		Vec3 vToWall;
		Math::VectorAngles(vWallAngles, vToWall);
		pCmd->viewangles = vToWall;
		pLocal->m_angRotation() = vToWall;

		// Set movement input to align with wall (DNA approach)
		float flRotation = Math::Deg2Rad(vToWall.y - vToWall.y); // Now 0 since we set viewangles
		float flCosRot = cos(flRotation);
		float flSinRot = sin(flRotation);
		pCmd->forwardmove = flCosRot * 10.f;
		pCmd->sidemove = -flSinRot * 10.f;
		pCmd->buttons |= IN_JUMP;
		pCmd->buttons |= IN_DUCK;

		// Debug: print movement inputs for first position
		if (iPositionsPassedGeometry == 1)
		{
			I::CVar->ConsolePrintf("  Movement inputs: forward=%.2f, side=%.2f, buttons=%d\n", 
				pCmd->forwardmove, pCmd->sidemove, pCmd->buttons);
		}

		// Use MovementSimulation for multi-tick prediction (doesn't restore state between ticks)
		MoveStorage tMoveStorage;
		tMoveStorage.m_pPlayer = pLocal;
		if (F::MoveSim.Initialize(pLocal, tMoveStorage, false, false))
		{
			F::MoveSim.SetDuck(tMoveStorage, true);
			
			// Run 16 ticks to let gravity accumulate
			for (int nTick = 0; nTick < 16; nTick++)
			{
				F::MoveSim.RunTick(tMoveStorage, false);
			}
			
			F::MoveSim.Restore(tMoveStorage);
			
			// Check if Z velocity matches pixelsurf velocity
			Vec3 vPredictedVelocity = tMoveStorage.m_MoveData.m_vecVelocity;
			float flVelocityDiff = fabs(vPredictedVelocity.z - flTargetZVel);
			
			// Strict check - must be exactly target velocity
			bool bIsPixelSurfVel = (flVelocityDiff < 0.01f);

			// Debug first few positions to see velocity values
			if (iPositionsPassedGeometry <= 5)
			{
				I::CVar->ConsolePrintf("  Pos %d: Pred Z vel = %.2f, Target = %.2f, Diff = %.2f\n", 
					iPositionsPassedGeometry, vPredictedVelocity.z, flTargetZVel, flVelocityDiff);
			}

			if (bIsPixelSurfVel)
			{
				iPositionsPassedVelocity++;
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
		else
		{
			// Fallback to single tick if MovementSimulation fails
			F::EnginePrediction.Start(pLocal, pCmd);
			F::EnginePrediction.End(pLocal, pCmd);
			
			Vec3 vPredictedVelocity = pLocal->m_vecVelocity();
			float flVelocityDiff = fabs(vPredictedVelocity.z - flTargetZVel);
			bool bIsPixelSurfVel = (flVelocityDiff < 0.5f);
			
			if (iPositionsPassedGeometry <= 5)
			{
				I::CVar->ConsolePrintf("  Pos %d (fallback): Pred Z vel = %.2f, Target = %.2f, Diff = %.2f\n", 
					iPositionsPassedGeometry, vPredictedVelocity.z, flTargetZVel, flVelocityDiff);
			}
			
			if (bIsPixelSurfVel)
			{
				iPositionsPassedVelocity++;
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
	}

	// Debug output
	I::CVar->ConsolePrintf("[PixelFinder] Detection Results:\n");
	I::CVar->ConsolePrintf("  Positions tested: %d\n", iPositionsTested);
	I::CVar->ConsolePrintf("  Passed geometry check: %d\n", iPositionsPassedGeometry);
	I::CVar->ConsolePrintf("  Passed velocity check: %d\n", iPositionsPassedVelocity);
	I::CVar->ConsolePrintf("  Points detected: %zu\n", vNewPoints.size());
	I::CVar->ConsolePrintf("  Target Z velocity: %.2f\n", flTargetZVel);
	I::CVar->ConsolePrintf("  Wall normal: (%.2f, %.2f, %.2f)\n", m_vWallNormal.x, m_vWallNormal.y, m_vWallNormal.z);
	I::CVar->ConsolePrintf("  Is displacement: %s\n", m_bIsDisplacement ? "yes" : "no");

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

	I::CVar->ConsolePrintf("[PixelFinder] Total points in buffer: %zu\n", m_vDetectedPoints.size());
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

	// Debug: log when rendering is called
	static int nRenderCallCount = 0;
	if (nRenderCallCount++ % 60 == 0)
	{
		I::CVar->ConsolePrintf("[PixelFinder] RenderPoints called, points in buffer: %zu\n", m_vDetectedPoints.size());
	}

	float flDeltaTime = I::GlobalVars->frametime;
	const float flAppearanceDuration = 0.5f;
	const float flDisappearanceDuration = 0.5f;
	const float flSizeAnimationSpeed = 5.0f;
	const float flNormalSize = 9.f;
	const float flInCrosshairSize = 12.f;

	// Update animations on the original points (not a copy)
	for (auto& point : m_vDetectedPoints)
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

	// Draw points from the buffer
	int nPointsDrawn = 0;
	int nPointsW2SFailed = 0;
	int nPointsOffscreen = 0;
	int nPointsZeroAlpha = 0;
	int nPointsZeroSize = 0;
	float flFirstPointSize = 0.f;
	float flFirstPointAlpha = 0.f;
	Color_t tFirstPointColor = Vars::Misc::PixelFinder::PointColor.Value;
	
	for (const auto& point : m_vDetectedPoints)
	{
		Vec3 vScreenPos;
		if (SDK::W2S(point.m_vPosition, vScreenPos))
		{
			// Validate screen coordinates
			if (!isfinite(vScreenPos.x) || !isfinite(vScreenPos.y))
			{
				nPointsW2SFailed++;
				continue;
			}
			if (vScreenPos.x < 0 || vScreenPos.x > H::Draw.m_nScreenW ||
				vScreenPos.y < 0 || vScreenPos.y > H::Draw.m_nScreenH)
			{
				nPointsOffscreen++;
				continue;
			}

			float flAlpha = point.m_flAnimationProgress * 255.f;
			Color_t tColor = Vars::Misc::PixelFinder::PointColor.Value;
			tColor.a = static_cast<byte>(flAlpha);

			// Capture first point stats for debug
			if (nPointsDrawn == 0 && nPointsW2SFailed == 0 && nPointsOffscreen == 0)
			{
				flFirstPointSize = point.m_flCurrentSize;
				flFirstPointAlpha = flAlpha;
				tFirstPointColor = tColor;
			}

			if (tColor.a > 0)
			{
				// Debug: check if size is too small to see
				if (point.m_flCurrentSize < 1.f)
				{
					nPointsZeroSize++;
				}
				
				// Draw circle
				H::Draw.LineCircle(vScreenPos.x, vScreenPos.y, point.m_flCurrentSize, 32, tColor);
				nPointsDrawn++;

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
			else
			{
				nPointsZeroAlpha++;
			}
		}
		else
		{
			nPointsW2SFailed++;
		}
	}
	
	// Debug render stats
	if (nRenderCallCount % 60 == 0)
	{
		I::CVar->ConsolePrintf("[PixelFinder] Render stats: drawn=%d, w2s_failed=%d, offscreen=%d, zero_alpha=%d, zero_size=%d\n", 
			nPointsDrawn, nPointsW2SFailed, nPointsOffscreen, nPointsZeroAlpha, nPointsZeroSize);
		I::CVar->ConsolePrintf("[PixelFinder] First point: size=%.2f, alpha=%.2f, color=(%d,%d,%d,%d)\n", 
			flFirstPointSize, flFirstPointAlpha, tFirstPointColor.r, tFirstPointColor.g, tFirstPointColor.b, tFirstPointColor.a);
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

	// Draw the line - use W2S for 2D rendering
	if (!vLineStart.IsZero() && !vLineEnd.IsZero())
	{
		Vec3 vStartScreen, vEndScreen;
		if (SDK::W2S(vLineStart, vStartScreen) && 
			SDK::W2S(vLineEnd, vEndScreen))
		{
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

PixelSurfPoint_t* CPixelFinder::FindNearestSavedPoint(CTFPlayer* pLocal)
{
	if (m_vSavedPoints.empty())
	{
		I::CVar->ConsolePrintf("[PixelSurf Assist] No saved points available\n");
		return nullptr;
	}

	Vec3 vPlayerPos = pLocal->m_vecOrigin();
	PixelSurfPoint_t* pNearest = nullptr;
	float flNearestDist = Vars::Misc::Movement::PixelSurfAssist::ActivationDistance.Value;
	std::string sCurrentMap = I::EngineClient->GetLevelName();

	I::CVar->ConsolePrintf("[PixelSurf Assist] Current map: %s, Saved points: %zu\n", sCurrentMap.c_str(), m_vSavedPoints.size());

	for (auto& point : m_vSavedPoints)
	{
		// Check if point is on current map
		if (point.m_sMap != sCurrentMap)
		{
			I::CVar->ConsolePrintf("[PixelSurf Assist] Skipping point - map mismatch: %s vs %s\n", point.m_sMap.c_str(), sCurrentMap.c_str());
			continue;
		}

		float flDist = vPlayerPos.DistTo(point.m_vPosition);
		I::CVar->ConsolePrintf("[PixelSurf Assist] Point distance: %.2f (max: %.2f)\n", flDist, flNearestDist);
		
		if (flDist < flNearestDist)
		{
			flNearestDist = flDist;
			pNearest = &point;
		}
	}

	if (pNearest)
		I::CVar->ConsolePrintf("[PixelSurf Assist] Found nearest point at distance %.2f\n", flNearestDist);
	else
		I::CVar->ConsolePrintf("[PixelSurf Assist] No point within activation distance\n");

	return pNearest;
}

void CPixelFinder::RunPixelSurfAssist(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	I::CVar->ConsolePrintf("[PixelSurf Assist] RunPixelSurfAssist called\n");
	
	// Find nearest saved pixelsurf point
	PixelSurfPoint_t* pTargetPoint = FindNearestSavedPoint(pLocal);
	if (!pTargetPoint)
		return;

	Vec3 vPlayerPos = pLocal->m_vecOrigin();
	Vec3 vTargetPos = pTargetPoint->m_vPosition;

	// Calculate height difference
	float flHeightDiff = vTargetPos.z - vPlayerPos.z;
	float flJumpOffset = Vars::Misc::Movement::PixelSurfAssist::JumpHeight.Value;
	float flDuckOffset = Vars::Misc::Movement::PixelSurfAssist::DuckHeight.Value;

	I::CVar->ConsolePrintf("[PixelSurf Assist] Height diff: %.2f, Jump offset: %.2f, Duck offset: %.2f\n", flHeightDiff, flJumpOffset, flDuckOffset);

	// Calculate direction to wall (from player to target)
	Vec3 vToTarget = vTargetPos - vPlayerPos;
	vToTarget.z = 0.f; // Only horizontal direction
	float flDist = vToTarget.Length();
	if (flDist < 1.f)
		return;

	vToTarget = vToTarget / flDist; // Normalize

	// Calculate strafe direction (perpendicular to wall normal)
	Vec3 vWallNormal = -vToTarget; // Wall normal points away from wall
	Vec3 vStrafeDir = { -vWallNormal.y, vWallNormal.x, 0.f };

	// Determine which strafe direction to use based on player's current velocity
	Vec3 vPlayerVel = pLocal->m_vecVelocity();
	vPlayerVel.z = 0.f;
	float flDot = vPlayerVel.Dot(vStrafeDir);
	if (flDot < 0.f)
		vStrafeDir = -vStrafeDir;

	I::CVar->ConsolePrintf("[PixelSurf Assist] To target: (%.2f, %.2f), Strafe dir: (%.2f, %.2f)\n", vToTarget.x, vToTarget.y, vStrafeDir.x, vStrafeDir.y);

	// Apply auto strafe if enabled
	if (Vars::Misc::Movement::PixelSurfAssist::AutoStrafe.Value)
	{
		// Convert direction vectors to movement inputs based on player view angles
		Vec3 vViewAngles = pLocal->m_angEyeAngles();
		float flYaw = Math::Deg2Rad(vViewAngles.y);
		
		float flForward = vToTarget.x * cos(flYaw) - vToTarget.y * sin(flYaw);
		float flSide = vToTarget.x * sin(flYaw) + vToTarget.y * cos(flYaw);
		
		float flStrafeForward = vStrafeDir.x * cos(flYaw) - vStrafeDir.y * sin(flYaw);
		float flStrafeSide = vStrafeDir.x * sin(flYaw) + vStrafeDir.y * cos(flYaw);
		
		pCmd->forwardmove = flForward * 450.f;
		pCmd->sidemove = flStrafeSide * 450.f;
		
		I::CVar->ConsolePrintf("[PixelSurf Assist] Applied movement - forward: %.2f, side: %.2f\n", pCmd->forwardmove, pCmd->sidemove);
	}

	// Calculate target height with offsets
	float flTargetHeight = vTargetPos.z + flJumpOffset - flDuckOffset;

	// Use jump if on ground and need to go up
	bool bOnGround = (pLocal->m_fFlags() & FL_ONGROUND);
	if (bOnGround && flHeightDiff > 5.f)
	{
		pCmd->buttons |= IN_JUMP;
		I::CVar->ConsolePrintf("[PixelSurf Assist] Applied IN_JUMP\n");
	}

	// Use duck if need to go down or to fine-tune height
	if (flHeightDiff < -2.f || (flHeightDiff > 0.f && flHeightDiff < 5.f))
	{
		pCmd->buttons |= IN_DUCK;
		I::CVar->ConsolePrintf("[PixelSurf Assist] Applied IN_DUCK\n");
	}

	// Visualize target height if enabled
	if (Vars::Misc::Movement::PixelSurfAssist::VisualizeTarget.Value)
	{
		Vec3 vScreenPos;
		if (SDK::W2S(vTargetPos, vScreenPos))
		{
			// Draw a line from player to target height
			Vec3 vPlayerScreen;
			Vec3 vPlayerTop = { vPlayerPos.x, vPlayerPos.y, flTargetHeight };
			if (SDK::W2S(vPlayerTop, vPlayerScreen))
			{
				Color_t tLineColor;
				tLineColor.SetRGB(0, 255, 100, 255);
				H::Draw.Line(vPlayerScreen.x, vPlayerScreen.y, vScreenPos.x, vScreenPos.y, tLineColor);
			}
		}
	}
}

void CPixelFinder::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive())
		return;

	// Run pixelsurf assist if enabled
	if (Vars::Misc::Movement::PixelSurfAssist::Enabled.Value)
	{
		RunPixelSurfAssist(pLocal, pCmd);
	}

	if (!Vars::Misc::PixelFinder::Enabled.Value)
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
