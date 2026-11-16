#pragma once

// Smooth error correction for remote entities: instead of teleporting to every received position, a part of the
// error is applied right away and the rest is spread over the next frames (on top of the entity's own movement)
namespace NetworkSmoothing
{
    constexpr float INSTANT_CORRECTION = 0.3f;
    constexpr float CATCH_UP_SECONDS = 0.1f;

    // returns the position the entity should be set to now
    inline CVector ApplyTarget(
        const CVector& current, const CVector& target, CVector& error, bool& hasError, float snapDistance)
    {
        CVector delta = target - current;
        float distance = delta.Magnitude();

        if (distance > snapDistance || distance < 0.03f)
        {
            hasError = false;
            return target;
        }

        error = delta * (1.0f - INSTANT_CORRECTION);
        hasError = true;
        return current + delta * INSTANT_CORRECTION;
    }

    // part of the remaining error to apply this frame
    inline float FrameFraction()
    {
        // CTimer::ms_fTimeStep is in 1/50 s units
        float fraction = CTimer::ms_fTimeStep * 0.02f / CATCH_UP_SECONDS;
        return fraction > 1.0f ? 1.0f : fraction;
    }

    inline CVector Step(CVector& error, bool& hasError)
    {
        CVector step = error * FrameFraction();
        error -= step;

        if (error.Magnitude() < 0.01f)
            hasError = false;

        return step;
    }
}
