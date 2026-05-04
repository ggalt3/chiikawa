#include "PixelFinder.h"

void CPixelFinder::DrawVerticalLine(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::PixelFinder::Enabled.Value || !pLocal || !pLocal->IsAlive())
		return;

	// Temporarily disable trace operations to isolate crash
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
			// Temporarily disable detection to test line drawing
			// DetectPixelSurfPoints(pLocal, pCmd);
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

	// Test points along the line
	float flStep = Vars::Misc::PixelFinder::DetectionStep.Value;
	int iMaxPoints = static_cast<int>(flLineLength / flStep);

	for (int i = 0; i <= iMaxPoints; i++)
	{
		float flLerp = (static_cast<float>(i) / iMaxPoints);
		Vec3 vTestPos = {
			m_vLineStart.x,
			m_vLineStart.y,
			m_vLineStart.z + (m_vLineEnd.z - m_vLineStart.z) * flLerp
		};

		// Offset position based on wall normal for player collision
		float flOffset = 16.0f;
		if (m_bIsDisplacement)
			flOffset = 16.031f;

		Vec3 vPlayerPos = vTestPos + m_vWallNormal * flOffset;
		vPlayerPos.z -= 54.0f; // Adjust for player eye height

		// Test if this position can pixelsurf
		if (TestPixelSurfAtPosition(pLocal, vPlayerPos, m_vWallNormal, pCmd))
		{
			PixelSurfPoint_t point;
			point.m_vPosition = vTestPos;
			point.m_sMap = I::EngineClient->GetLevelName();
			point.m_flAnimationProgress = 0.f;
			point.m_bIsAppearing = true;
			point.m_bIsRemoving = false;
			point.m_flCurrentSize = 0.f;
			m_vDetectedPoints.push_back(point);
		}
	}
	m_bClearingPoints = false;
}

bool CPixelFinder::TestPixelSurfAtPosition(CTFPlayer* pLocal, const Vec3& vPosition, const Vec3& vWallNormal, CUserCmd* pCmd)
{
	// Trace down to check if we're on ground at this position
	Vec3 vStart = vPosition + Vec3(0, 0, 54.f);
	Vec3 vEnd = vPosition - Vec3(0, 0, 1000.f);

	CGameTrace traceDown = {};
	Ray_t rayDown;
	rayDown.Init(vStart, vEnd);
	CTraceFilterWorldAndPropsOnly filterDown;
	filterDown.pSkip = pLocal;
	I::EngineTrace->TraceRay(rayDown, MASK_SOLID, &filterDown, &traceDown);

	if (!traceDown.DidHit())
		return false;

	// Check if we're too close to ceiling
	Vec3 vTraceUpStart = traceDown.endpos + Vec3(0, 0, 2.f);
	Vec3 vTraceUpEnd = vTraceUpStart + Vec3(0, 0, 100.f);

	CGameTrace traceUp = {};
	Ray_t rayUp;
	rayUp.Init(vTraceUpStart, vTraceUpEnd);
	CTraceFilterWorldAndPropsOnly filterUp;
	filterUp.pSkip = pLocal;
	I::EngineTrace->TraceRay(rayUp, MASK_SOLID, &filterUp, &traceUp);

	if (traceUp.DidHit() && traceUp.fraction < 0.1f)
		return false;

	// Get gravity for velocity check
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	if (!sv_gravity)
		return false;

	float flGravity = sv_gravity->GetFloat();
	float flTickRate = 1.0f / I::GlobalVars->interval_per_tick;
	float flTargetZVel = -((flGravity / 2.0f) / flTickRate);

	// Check if the position is at the right height for pixelsurf
	// Pixelsurf works when you're at a specific height where the velocity from gravity matches the pixelsurf threshold
	float flHeightAboveGround = vPosition.z - traceDown.endpos.z;
	
	// Calculate expected velocity at this height
	float flExpectedVel = -sqrt(2.0f * flGravity * flHeightAboveGround);
	
	// Check if velocity is in the pixelsurf range (typically around -200 to -300 for pixelsurf)
	bool bCanPixelSurf = fabs(flExpectedVel - flTargetZVel) < 50.0f;

	return bCanPixelSurf;
}

void CPixelFinder::RenderPoints()
{
	if (!Vars::Misc::PixelFinder::Enabled.Value)
		return;

	float flDeltaTime = I::GlobalVars->frametime;
	const float flAppearanceDuration = 0.5f;
	const float flDisappearanceDuration = 0.5f;
	const float flSizeAnimationSpeed = 5.0f;
	const float flNormalSize = 9.f;
	const float flInCrosshairSize = 12.f;

	// Update animations
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

	// Remove points that are done disappearing
	m_vDetectedPoints.erase(
		std::remove_if(m_vDetectedPoints.begin(), m_vDetectedPoints.end(),
			[](const PixelSurfPoint_t& point) { return point.m_bIsRemoving && point.m_flAnimationProgress <= 0.f; }),
		m_vDetectedPoints.end());

	// Draw points
	for (const auto& point : m_vDetectedPoints)
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
	if (!Vars::Misc::PixelFinder::Enabled.Value || m_bClearingPoints)
		return;

	for (const auto& point : m_vSavedPoints)
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
	// Temporarily disable rendering to isolate crash
	return;
	
	if (!Vars::Misc::PixelFinder::Enabled.Value)
		return;

	// Draw the line after detection is complete
	if (!m_bDrawingLine && !m_vLineStart.IsZero() && !m_vLineEnd.IsZero())
	{
		Vec3 vStartScreen, vEndScreen;
		if (SDK::W2S(m_vLineStart, vStartScreen) && 
			SDK::W2S(m_vLineEnd, vEndScreen))
		{
			Color_t tLineColor = Vars::Misc::PixelFinder::LineColor.Value;
			if (tLineColor.a > 0)
			{
				H::Draw.Line(vStartScreen.x, vStartScreen.y, vEndScreen.x, vEndScreen.y, tLineColor);
			}
		}
	}

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

	// Find the point in crosshair and save it
	for (const auto& point : m_vDetectedPoints)
	{
		if (point.m_bInCrosshair)
		{
			// Check if point already exists
			bool bExists = false;
			for (const auto& saved : m_vSavedPoints)
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
				m_vSavedPoints.push_back(newPoint);
				
				// Clear detected points after saving
				m_vDetectedPoints.clear();
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
	m_vSavedPoints.clear();
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
