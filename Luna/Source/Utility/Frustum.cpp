#include <Luna/Utility/Frustum.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _MSC_VER
#	include <intrin.h>
#	if defined(_INCLUDED_PMM) && !defined(__SSE3__)
#		define __SSE3__ 1
#	endif
#	if !defined(__SSE__)
#		define __SSE__ 1
#	endif
#	if defined(_INCLUDED_IMM) && !defined(__AVX__)
#		define __AVX__ 1
#	endif
#elif defined(__AVX__)
#	include <immintrin.h>
#elif defined(__SSE3__)
#	include <pmmintrin.h>
#elif defined(__SSE__)
#	include <xmmintrin.h>
#elif defined(__ARM__NEON)
#	include <arm_neon.h>
#endif

namespace Luna {
bool Frustum::Contains(const AABB& aabb) const {
#if defined(__SSE3__)
	__m128 lo = _mm_loadu_ps(glm::value_ptr(aabb.Min4()));
	__m128 hi = _mm_loadu_ps(glm::value_ptr(aabb.Max4()));

#	define ComputePlane(i)                                                                 \
		__m128 p##i         = _mm_loadu_ps(glm::value_ptr(_planes[i]));                       \
		__m128 mask##i      = _mm_cmpgt_ps(p##i, _mm_setzero_ps());                           \
		__m128 majorAxis##i = _mm_or_ps(_mm_and_ps(mask##i, hi), _mm_andnot_ps(mask##i, lo)); \
		__m128 dotted##i    = _mm_mul_ps(p##i, majorAxis##i)
	ComputePlane(0);
	ComputePlane(1);
	ComputePlane(2);
	ComputePlane(3);
	ComputePlane(4);
	ComputePlane(5);
#	undef ComputePlane

	__m128 merged01   = _mm_hadd_ps(dotted0, dotted1);
	__m128 merged23   = _mm_hadd_ps(dotted2, dotted3);
	__m128 merged45   = _mm_hadd_ps(dotted4, dotted5);
	merged45          = _mm_hadd_ps(merged45, merged45);
	__m128 merged0123 = _mm_hadd_ps(merged01, merged23);
	__m128 merged     = _mm_or_ps(merged0123, merged45);

	int mask = _mm_movemask_ps(merged);

	return mask == 0;
#else
#	error "Frustum::Contains has no SIMD impl."
#endif
}

bool Frustum::Intersect(const AABB& aabb) const {
	for (auto& plane : _planes) {
		bool intersectsPlane = false;
		for (uint32_t i = 0; i < 8; ++i) {
			if (glm::dot(glm::vec4(aabb.GetCorner(i), 1.0f), plane) >= 0.0f) {
				intersectsPlane = true;
				break;
			}
		}

		if (!intersectsPlane) { return false; }
	}

	return true;
}

bool Frustum::IntersectSphere(const AABB& aabb) const {
	const glm::vec4 center(aabb.GetCenter(), 1.0f);
	const float radius = aabb.GetRadius();

	for (auto& plane : _planes) {
		if (glm::dot(plane, center) < -radius) { return false; }
	}

	return true;
}

void Frustum::BuildPlanes(const glm::mat4& invViewProjection) {
	constexpr static const glm::vec4 tln(-1, -1, 0, 1);
	constexpr static const glm::vec4 tlf(-1, -1, 1, 1);
	constexpr static const glm::vec4 bln(-1, 1, 0, 1);
	constexpr static const glm::vec4 blf(-1, 1, 1, 1);
	constexpr static const glm::vec4 trn(1, -1, 0, 1);
	constexpr static const glm::vec4 trf(1, -1, 1, 1);
	constexpr static const glm::vec4 brn(1, 1, 0, 1);
	constexpr static const glm::vec4 brf(1, 1, 1, 1);
	constexpr static const glm::vec4 c(0, 0, 0.5, 1);

	_invViewProjection = invViewProjection;

	const auto Project  = [](const glm::vec4& v) { return glm::vec3(v) / v.w; };
	const glm::vec3 TLN = Project(_invViewProjection * tln);
	const glm::vec3 BLN = Project(_invViewProjection * bln);
	const glm::vec3 BLF = Project(_invViewProjection * blf);
	const glm::vec3 TRN = Project(_invViewProjection * trn);
	const glm::vec3 TRF = Project(_invViewProjection * trf);
	const glm::vec3 BRN = Project(_invViewProjection * brn);
	const glm::vec3 BRF = Project(_invViewProjection * brf);

	const glm::vec3 l = glm::normalize(glm::cross(BLF - BLN, TLN - BLN));
	const glm::vec3 r = glm::normalize(glm::cross(TRF - TRN, BRN - TRN));
	const glm::vec3 n = glm::normalize(glm::cross(BLN - BRN, TRN - BRN));
	const glm::vec3 f = glm::normalize(glm::cross(TRF - BRF, BLF - BRF));
	const glm::vec3 t = glm::normalize(glm::cross(TLN - TRN, TRF - TRN));
	const glm::vec3 b = glm::normalize(glm::cross(BRF - BRN, BLN - BRN));

	_planes[0] = glm::vec4(l, -glm::dot(l, BLN));
	_planes[1] = glm::vec4(r, -glm::dot(r, TRN));
	_planes[2] = glm::vec4(n, -glm::dot(n, BRN));
	_planes[3] = glm::vec4(f, -glm::dot(f, BRF));
	_planes[4] = glm::vec4(t, -glm::dot(t, TRN));
	_planes[5] = glm::vec4(b, -glm::dot(b, BRN));

	const glm::vec4 center = _invViewProjection * c;
	for (auto& p : _planes) {
		if (glm::dot(center, p) < 0.0f) { p = -p; }
	}
}
}  // namespace Luna
