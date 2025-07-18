#pragma once
/**
	@file
	@brief elliptic curve
	@author MITSUNARI Shigeo(@herumi)
	@license modified new BSD license
	http://opensource.org/licenses/BSD-3-Clause
*/
#include <stdlib.h>
#include <mcl/fp_def.hpp>
#include <mcl/fr_def.hpp>
#include <mcl/ecparam.hpp>
#include <mcl/window_method.hpp>

#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable : 4458)
#endif
#ifdef MCL_USE_OMP
#include <omp.h>
#endif

namespace mcl {

namespace ec {

enum Mode {
	Jacobi = 0,
	Proj = 1,
	Affine
};

namespace local {

enum ModeCoeffA {
	Zero,
	Minus3,
	GenericA
};

enum ModeCoeffB {
	Plus1,
	Plus4,
	GenericB
};

template<class Ec, class Vec>
void addTbl(Ec& Q, const Ec *tbl, const Vec& naf, size_t i)
{
	if (i >= naf.size()) return;
	int n = naf[i];
	if (n > 0) {
		Q += tbl[(n - 1) >> 1];
	} else if (n < 0) {
		Q -= tbl[(-n - 1) >> 1];
	}
}

/*
	elliptic class E must have
	member variables of type Fp x, y, z
	static member a_, b_, specialA_
*/
// x is negative <=> x < half(:=(p+1)/2) <=> a = 1
inline bool get_a_flag(const Fp& x)
{
	return x.isNegative();
}

// Im(x) is negative <=> Im(x) < half(:=(p+1)/2) <=> a = 1

template<class _Fp2>
inline bool get_a_flag(const _Fp2& x)
{
	return get_a_flag(x.b); // x = a + bi
}

template<class F>
void mul3(F& x)
{
	F t;
	F::add(t, x, x);
	F::add(x, t, x); // 3x
}

template<class F>
void mul4(F& x)
{
	F::add(x, x, x);
	F::add(x, x, x);
}

template<class F>
void mul12(F& x)
{
	F t;
	F::add(t, x, x);
	F::add(x, t, x); // 3x
	mul4(x);
}

// AsArrayOfFp[i] gets P[i].z
template<class E>
struct AsArrayOfFp {
	typedef typename E::Fp Fp;
	const E* P;
	AsArrayOfFp(const E* P) : P(P) {}
	const Fp& operator[](size_t i) const { return P[i].z; }
	void operator+=(size_t n) { P += n; }
};

template<class E>
void _normalizeJacobi(E& Q, const E& P, typename E::Fp& inv)
{
	typedef typename E::Fp F;
	F inv2;
	F::sqr(inv2, inv);
	F::mul(Q.x, P.x, inv2);
	F::mul(Q.y, P.y, inv2);
	Q.y *= inv;
	Q.z = 1;
}

template<class E>
void _normalizeProj(E& Q, const E& P, typename E::Fp& inv)
{
	typedef typename E::Fp F;
	F::mul(Q.x, P.x, inv);
	F::mul(Q.y, P.y, inv);
	Q.z = 1;
}

template<class E>
void _normalize(E& Q, const E& P, typename E::Fp& inv)
{
	switch (E::mode_) {
	case ec::Jacobi:
		_normalizeJacobi(Q, P, inv);
		break;
	case ec::Proj:
		_normalizeProj(Q, P, inv);
		break;
	default:
		assert(0);
		break;
	}
}

/*
	Q[i] = normalize(P[i]) for i = 0, ..., n-1
	AsArray : Pz[i] to access like as F[i] in invVecT
	N : alloc size
*/
template<class F, class Eout, class Ein, class AsArrayOfFp>
void normalizeVecT(Eout& Q, Ein& P, size_t n, size_t N = 256)
{
	F *inv = (F*)CYBOZU_ALLOCA(sizeof(F) * N);
	bool PisEqualToQ = &P[0] == &Q[0];
	for (;;) {
		size_t doneN = (n < N) ? n : N;
		AsArrayOfFp Pz(P);
		invVecT<F>(inv, Pz, doneN, N);
		for (size_t i = 0; i < doneN; i++) {
			if (P[i].z.isZero() || P[i].z.isOne()) {
				if (!PisEqualToQ) Q[i] = P[i];
			} else {
				local::_normalize(Q[i], P[i], inv[i]);
			}
		}
		n -= doneN;
		if (n == 0) return;
		Q += doneN;
		P += doneN;
	}
}

} // mcl::ec::local

// [X:Y:Z] as Proj = (X/Z, Y/Z) as Affine = [XZ:YZ^2:Z] as Jacobi
// Remark. convert P = [*:*:0] to Q = [0:0:0]
template<class E>
void ProjToJacobi(E& Q, const E& P)
{
	typedef typename E::Fp F;
	F::mul(Q.x, P.x, P.z);
	F::mul(Q.y, P.y, P.z);
	F::mul(Q.y, Q.y, P.z);
	Q.z = P.z;
}

// [X:Y:Z] as Jacobi = (X/Z^2, Y/Z^3) as Affine = [XZ:Y:Z^3] as Proj
// Remark. convert P = [*:1:0] to Q = [0:1:0]
template<class E>
void JacobiToProj(E& Q, const E& P)
{
	typedef typename E::Fp F;
	F::mul(Q.x, P.x, P.z);
	Q.y = P.y;
	F t;
	F::sqr(t, P.z);
	F::mul(Q.z, P.z, t);
}

template<class E>
void normalizeJacobi(E& P)
{
	if (P.z.isZero() || P.z.isOne()) return;
	typedef typename E::Fp F;
	F::inv(P.z, P.z);
	local::_normalizeJacobi(P, P, P.z);
}

/*
	Q[i] = normalize(P[i]) for i = 0, ..., n-1
	AsArray : Pz[i] to access like as F[i] in invVecT
	N : alloc size
*/
template<class E>
void normalizeVec(E *Q, const E *P, size_t n)
{
	local::normalizeVecT<typename E::Fp, E*, const E*, local::AsArrayOfFp<E> >(Q, P, n);
}

// (x/z^2, y/z^3)
template<class E>
bool isEqualJacobi(const E& P1, const E& P2)
{
	typedef typename E::Fp F;
	bool zero1 = P1.isZero();
	bool zero2 = P2.isZero();
	if (zero1) {
		return zero2;
	}
	if (zero2) return false;
	F s1, s2, t1, t2;
	F::sqr(s1, P1.z);
	F::sqr(s2, P2.z);
	F::mul(t1, P1.x, s2);
	F::mul(t2, P2.x, s1);
	if (t1 != t2) return false;
	F::mul(t1, P1.y, s2);
	F::mul(t2, P2.y, s1);
	t1 *= P2.z;
	t2 *= P1.z;
	return t1 == t2;
}

// return (P1 == P2) ? 1 : (P1 == -P2) ? -1 : 0
template<class E>
int isEqualOrMinusJacobi(const E& P1, const E& P2)
{
	typedef typename E::Fp F;
	bool zero1 = P1.isZero();
	bool zero2 = P2.isZero();
	if (zero1) {
		return zero2 ? 1 : 0;
	}
	if (zero2) return 0;
	F s1, s2, t1, t2;
	F::sqr(s1, P1.z);
	F::sqr(s2, P2.z);
	F::mul(t1, P1.x, s2);
	F::mul(t2, P2.x, s1);
	if (t1 != t2) return 0;
	F::mul(t1, P1.y, s2);
	F::mul(t2, P2.y, s1);
	t1 *= P2.z;
	t2 *= P1.z;
	if (t1 == t2) return 1;
	F::neg(t1, t1);
	if (t1 == t2) return -1;
	return 0;
}

// Y^2 == X(X^2 + aZ^4) + bZ^6
template<class E>
bool isValidJacobi(const E& P)
{
	typedef typename E::Fp F;
	F y2, x2, z2, z4, t;
	F::sqr(x2, P.x);
	F::sqr(y2, P.y);
	F::sqr(z2, P.z);
	F::sqr(z4, z2);
	F::mul(t, z4, E::a_);
	t += x2;
	t *= P.x;
	z4 *= z2;
	if (E::specialB_ == ec::local::Plus1) {
		// pass
	} else
	if (E::specialB_ == ec::local::Plus4) {
		local::mul4(z4);
	} else
	{
		z4 *= E::b_;
	}
	t += z4;
	return y2 == t;
}

/*
	M > S + A
	a = 0   2M + 5S + 14A
	a = -3  2M + 7S + 15A
	generic 3M + 7S + 15A
	M == S
	a = 0   3M + 4S + 13A
	a = -3  3M + 6S + 14A
	generic 4M + 6S + 14A
*/
template<class E>
void dblJacobi(E& R, const E& P)
{
	typedef typename E::Fp F;
	if (P.isZero()) {
		R.clear();
		return;
	}
	const bool isPzOne = P.z.isOne();
	F x2, y2, xy, t;
	F::sqr(x2, P.x);
	F::sqr(y2, P.y);
	if (sizeof(F) <= 32) { // M == S
		F::mul(xy, P.x, y2);
		xy += xy;
		F::sqr(y2, y2);
	} else { // M > S + A
		F::add(xy, P.x, y2);
		F::sqr(y2, y2);
		F::sqr(xy, xy);
		xy -= x2;
		xy -= y2;
	}
	xy += xy; // 4xy^2
	switch (E::specialA_) {
	case local::Zero:
		F::mul2(t, x2);
		x2 += t;
		break;
	case local::Minus3:
		if (isPzOne) {
			x2 -= P.z;
		} else {
			F::sqr(t, P.z);
			F::sqr(t, t);
			x2 -= t;
		}
		F::mul2(t, x2);
		x2 += t;
		break;
	case local::GenericA:
	default:
		if (isPzOne) {
			t = E::a_;
		} else {
			F::sqr(t, P.z);
			F::sqr(t, t);
			t *= E::a_;
		}
		t += x2;
		F::mul2(x2, x2);
		x2 += t;
		break;
	}
	F::sqr(R.x, x2);
	R.x -= xy;
	R.x -= xy;
	if (isPzOne) {
		R.z = P.y;
	} else {
		F::mul(R.z, P.y, P.z);
	}
	F::mul2(R.z, R.z);
	F::sub(R.y, xy, R.x);
	R.y *= x2;
	F::mul2(y2, y2);
	F::mul2(y2, y2);
	F::mul2(y2, y2);
	R.y -= y2;
}

/*
	J + J : 12mul + 4sqr + 7add
	J + A : 8mul + 3sqr + 7add
	A + A : 4mul + 2sqr + 7add
*/
template<class E>
void addJacobi(E& R, const E& P, const E& Q)
{
	typedef typename E::Fp F;
	if (P.isZero()) { R = Q; return; }
	if (Q.isZero()) { R = P; return; }
	bool isPzOne = P.z.isOne();
	bool isQzOne = Q.z.isOne();
	F r, U1, S1, H, H3;
	if (isPzOne) {
		// r = 1;
	} else {
		F::sqr(r, P.z);
	}
	if (isQzOne) {
		U1 = P.x;
		if (isPzOne) {
			H = Q.x;
		} else {
			F::mul(H, Q.x, r);
		}
		H -= U1;
		S1 = P.y;
	} else {
		F::sqr(S1, Q.z);
		F::mul(U1, P.x, S1);
		if (isPzOne) {
			H = Q.x;
		} else {
			F::mul(H, Q.x, r);
		}
		H -= U1;
		S1 *= Q.z;
		S1 *= P.y;
	}
	if (isPzOne) {
		r = Q.y;
	} else {
		r *= P.z;
		r *= Q.y;
	}
	r -= S1;
	if (H.isZero()) {
		if (r.isZero()) {
			ec::dblJacobi(R, P);
		} else {
			R.clear();
		}
		return;
	}
	if (isPzOne) {
		if (isQzOne) {
			R.z = H;
		} else {
			F::mul(R.z, H, Q.z);
		}
	} else {
		if (isQzOne) {
			F::mul(R.z, P.z, H);
		} else {
			F::mul(R.z, P.z, Q.z);
			R.z *= H;
		}
	}
	F::sqr(H3, H); // H^2
	F::sqr(R.y, r); // r^2
	U1 *= H3; // U1 H^2
	H3 *= H; // H^3
	R.y -= U1;
	R.y -= U1;
	F::sub(R.x, R.y, H3);
	U1 -= R.x;
	U1 *= r;
	H3 *= S1;
	F::sub(R.y, U1, H3);
}

/*
	accept P == Q
	https://eprint.iacr.org/2015/1060
	(x, y, z) is zero <=> x = 0, y = 1, z = 0
*/

// (b=4) 12M+27A
// (generic) 14M+19A
// Q.z = 1 if mixed
template<class E>
void addCTProj(E& R, const E& P, const E& Q, bool mixed = false)
{
	typedef typename E::Fp F;
	assert(E::a_ == 0);
	F t0, t1, t2, t3, t4, x3, y3;
	F::mul(t0, P.x, Q.x);
	F::mul(t1, P.y, Q.y);
	if (mixed) {
		t2 = P.z;
	} else {
		F::mul(t2, P.z, Q.z);
	}
	F::add(t3, P.x, P.y);
	F::add(t4, Q.x, Q.y);
	F::mul(t3, t3, t4);
	F::add(t4, t0, t1);
	F::sub(t3, t3, t4);
	F::add(t4, P.y, P.z);
	F::add(x3, Q.y, Q.z);
	F::mul(t4, t4, x3);
	F::add(x3, t1, t2);
	F::sub(t4, t4, x3);
	F::add(x3, P.x, P.z);
	F::add(y3, Q.x, Q.z);
	F::mul(x3, x3, y3);
	F::add(y3, t0, t2);
	F::sub(y3, x3, y3);
	F::add(x3, t0, t0);
	F::add(t0, t0, x3);
	if (E::specialB_ == ec::local::Plus1) {
		local::mul3(t2);
	} else
	if (E::specialB_ == ec::local::Plus4) {
		local::mul12(t2);
	} else
	{
		F::mul(t2, t2, E::b3_);
	}
	F::add(R.z, t1, t2);
	F::sub(t1, t1, t2);
	if (E::specialB_ == ec::local::Plus1) {
		local::mul3(y3);
	} else
	if (E::specialB_ == ec::local::Plus4) {
		local::mul12(y3);
	} else
	{
		F::mul(y3, y3, E::b3_);
	}
	F::mul(x3, y3, t4);
	F::mul(t2, t3, t1);
	F::sub(R.x, t2, x3);
	F::mul(y3, y3, t0);
	F::mul(t1, t1, R.z);
	F::add(R.y, y3, t1);
	F::mul(t0, t0, t3);
	F::mul(R.z, R.z, t4);
	F::add(R.z, R.z, t0);
}
// (b = 4) 6M+2S+13A
// (generic) 7M+2S+9A
template<class E>
void dblCTProj(E& R, const E& P)
{
	typedef typename E::Fp F;
	assert(E::a_ == 0);
	F t0, t1, t2, x3, y3;
	F::sqr(t0, P.y);
	F::mul(t1, P.y, P.z);
	F::sqr(t2, P.z);
	F::add(R.z, t0, t0);
	F::add(R.z, R.z, R.z);
	F::add(R.z, R.z, R.z);
	if (E::specialB_ == ec::local::Plus1) {
		local::mul3(t2);
	} else
	if (E::specialB_ == ec::local::Plus4) {
		local::mul12(t2);
	} else
	{
		F::mul(t2, t2, E::b3_);
	}
	F::mul(x3, t2, R.z);
	F::add(y3, t0, t2);
	F::mul(R.z, R.z, t1);
	F::add(t1, t2, t2);
	F::add(t2, t2, t1);
	F::mul(t1, P.x, P.y);
	F::sub(t0, t0, t2);
	F::mul(R.y, y3, t0);
	F::add(R.y, R.y, x3);
	F::mul(R.x, t0, t1);
	F::add(R.x, R.x, R.x);
}

template<class E>
void normalizeProj(E& P)
{
	if (P.z.isZero() || P.z.isOne()) return;
	typedef typename E::Fp F;
	F::inv(P.z, P.z);
	local::_normalizeProj(P, P, P.z);
}

// (Y^2 - bZ^2)Z = X(X^2 + aZ^2)
template<class E>
bool isValidProj(const E& P)
{
	typedef typename E::Fp F;
	F y2, x2, z2, t;
	F::sqr(x2, P.x);
	F::sqr(y2, P.y);
	F::sqr(z2, P.z);
	F::mul(t, E::a_, z2);
	t += x2;
	t *= P.x;
	z2 *= E::b_;
	y2 -= z2;
	y2 *= P.z;
	return y2 == t;
}

// (x/z, y/z)
template<class E>
bool isEqualProj(const E& P1, const E& P2)
{
	typedef typename E::Fp F;
	bool zero1 = P1.isZero();
	bool zero2 = P2.isZero();
	if (zero1) {
		return zero2;
	}
	if (zero2) return false;
	F t1, t2;
	F::mul(t1, P1.x, P2.z);
	F::mul(t2, P2.x, P1.z);
	if (t1 != t2) return false;
	F::mul(t1, P1.y, P2.z);
	F::mul(t2, P2.y, P1.z);
	return t1 == t2;
}

// return (P1 == P2) ? 1 : (P1 == -P2) ? -1 : 0
template<class E>
int isEqualOrMinusProj(const E& P1, const E& P2)
{
	typedef typename E::Fp F;
	bool zero1 = P1.isZero();
	bool zero2 = P2.isZero();
	if (zero1) {
		return zero2 ? 1 : 0;
	}
	if (zero2) return 0;
	F t1, t2;
	F::mul(t1, P1.x, P2.z);
	F::mul(t2, P2.x, P1.z);
	if (t1 != t2) return 0;
	F::mul(t1, P1.y, P2.z);
	F::mul(t2, P2.y, P1.z);
	if (t1 == t2) return 1;
	F::neg(t1, t1);
	if (t1 == t2) return -1;
	return 0;
}

/*
	   |a=0|-3| generic
	mul|  8| 8| 9
	sqr|  4| 5| 5
	add| 11|12|12
*/
template<class E>
void dblProj(E& R, const E& P)
{
	typedef typename E::Fp F;
	if (P.isZero()) {
		R.clear();
		return;
	}
	const bool isPzOne = P.z.isOne();
	F w, t, h;
	switch (E::specialA_) {
	case local::Zero:
		F::sqr(w, P.x);
		F::add(t, w, w);
		w += t;
		break;
	case local::Minus3:
		F::sqr(w, P.x);
		if (isPzOne) {
			w -= P.z;
		} else {
			F::sqr(t, P.z);
			w -= t;
		}
		F::add(t, w, w);
		w += t;
		break;
	case local::GenericA:
	default:
		if (isPzOne) {
			w = E::a_;
		} else {
			F::sqr(w, P.z);
			w *= E::a_;
		}
		F::sqr(t, P.x);
		w += t;
		w += t;
		w += t; // w = a z^2 + 3x^2
		break;
	}
	if (isPzOne) {
		R.z = P.y;
	} else {
		F::mul(R.z, P.y, P.z); // s = yz
	}
	F::mul(t, R.z, P.x);
	t *= P.y; // xys
	t += t;
	t += t; // 4(xys) ; 4B
	F::sqr(h, w);
	h -= t;
	h -= t; // w^2 - 8B
	F::mul(R.x, h, R.z);
	t -= h; // h is free
	t *= w;
	F::sqr(w, P.y);
	R.x += R.x;
	R.z += R.z;
	F::sqr(h, R.z);
	w *= h;
	R.z *= h;
	F::sub(R.y, t, w);
	R.y -= w;
}

/*
	mul| 12
	sqr|  2
	add|  7
*/
template<class E>
void addProj(E& R, const E& P, const E& Q)
{
	typedef typename E::Fp F;
	if (P.isZero()) { R = Q; return; }
	if (Q.isZero()) { R = P; return; }
	bool isPzOne = P.z.isOne();
	bool isQzOne = Q.z.isOne();
	F r, PyQz, v, A, vv;
	if (isQzOne) {
		r = P.x;
		PyQz = P.y;
	} else {
		F::mul(r, P.x, Q.z);
		F::mul(PyQz, P.y, Q.z);
	}
	if (isPzOne) {
		A = Q.y;
		v = Q.x;
	} else {
		F::mul(A, Q.y, P.z);
		F::mul(v, Q.x, P.z);
	}
	v -= r;
	if (v.isZero()) {
		if (A == PyQz) {
			dblProj(R, P);
		} else {
			R.clear();
		}
		return;
	}
	F::sub(R.y, A, PyQz);
	F::sqr(A, R.y);
	F::sqr(vv, v);
	r *= vv;
	vv *= v;
	if (isQzOne) {
		R.z = P.z;
	} else {
		if (isPzOne) {
			R.z = Q.z;
		} else {
			F::mul(R.z, P.z, Q.z);
		}
	}
	// R.z = 1 if isPzOne && isQzOne
	if (isPzOne && isQzOne) {
		R.z = vv;
	} else {
		A *= R.z;
		R.z *= vv;
	}
	A -= vv;
	vv *= PyQz;
	A -= r;
	A -= r;
	F::mul(R.x, v, A);
	r -= A;
	R.y *= r;
	R.y -= vv;
}

// y^2 == (x^2 + a)x + b
template<class E>
bool isValidAffine(const E& P)
{
	typedef typename E::Fp F;
	assert(!P.z.isZero());
	F y2, t;
	F::sqr(y2, P.y);
	F::sqr(t, P.x);
	t += E::a_;
	t *= P.x;
	t += E::b_;
	return y2 == t;
}

// y^2 = x^3 + ax + b
template<class E>
static inline void dblAffine(E& R, const E& P)
{
	typedef typename E::Fp F;
	if (P.isZero()) {
		R.clear();
		return;
	}
	if (P.y.isZero()) {
		R.clear();
		return;
	}
	F t, s;
	F::sqr(t, P.x);
	F::add(s, t, t);
	t += s;
	t += E::a_;
	F::add(s, P.y, P.y);
	t /= s;
	F::sqr(s, t);
	s -= P.x;
	F x3;
	F::sub(x3, s, P.x);
	F::sub(s, P.x, x3);
	s *= t;
	F::sub(R.y, s, P.y);
	R.x = x3;
	R.z = 1;
}

template<class E>
void addAffine(E& R, const E& P, const E& Q)
{
	typedef typename E::Fp F;
	if (P.isZero()) { R = Q; return; }
	if (Q.isZero()) { R = P; return; }
	F t;
	F::sub(t, Q.x, P.x);
	if (t.isZero()) {
		if (P.y == Q.y) {
			dblAffine(R, P);
		} else {
			R.clear();
		}
		return;
	}
	F s;
	F::sub(s, Q.y, P.y);
	F::div(t, s, t);
	R.z = 1;
	F x3;
	F::sqr(x3, t);
	x3 -= P.x;
	x3 -= Q.x;
	F::sub(s, P.x, x3);
	s *= t;
	F::sub(R.y, s, P.y);
	R.x = x3;
}

template<class E>
void tryAndIncMapTo(E& P, const typename E::Fp& t)
{
	typedef typename E::Fp F;
	F x = t;
	for (;;) {
		F y;
		E::getWeierstrass(y, x);
		if (F::squareRoot(y, y)) {
			bool b;
			P.set(&b, x, y, false);
			assert(b);
			return;
		}
		*x.getFp0() += F::BaseFp::one();
	}
}

template<class G>
bool mulSmallInt(G& z, const G& x, Unit y, bool isNegative)
{
	switch (y) {
	case 0: z.clear(); return true;
	case 1: z = x; break;
	case 2: G::dbl(z, x); break;
	case 3: {
		G t;
		G::dbl(t, x);
		G::add(z, t, x);
		break;
	}
	case 4: {
		G::dbl(z, x);
		G::dbl(z, z);
		break;
	}
	case 5: {
		G t;
		G::dbl(t, x);
		G::dbl(t, t);
		G::add(z, t, x);
		break;
	}
	case 6: {
		G t;
		G::dbl(t, x);
		G::add(z, t, x);
		G::dbl(z, z);
		break;
	}
	case 7: {
		G t;
		G::dbl(t, x);
		G::dbl(t, t);
		G::dbl(t, t);
		G::sub(z, t, x);
		break;
	}
	case 8: {
		G::dbl(z, x);
		G::dbl(z, z);
		G::dbl(z, z);
		break;
	}
	case 9: {
		G t;
		G::dbl(t, x);
		G::dbl(t, t);
		G::dbl(t, t);
		G::add(z, t, x);
		break;
	}
	case 10: {
		G t;
		G::dbl(t, x);
		G::dbl(t, t);
		G::add(z, t, x);
		G::dbl(z, z);
		break;
	}
	case 11: {
		G t1, t2;
		G::dbl(t1, x); // 2x
		G::dbl(t2, t1);
		G::dbl(t2, t2); // 8x
		G::add(t2, t2, t1);
		G::add(z, t2, x);
		break;
	}
	case 12: {
		G t1, t2;
		G::dbl(t1, x);
		G::dbl(t1, t1); // 4x
		G::dbl(t2, t1); // 8x
		G::add(z, t1, t2);
		break;
	}
	case 13: {
		G t1, t2;
		G::dbl(t1, x);
		G::dbl(t1, t1); // 4x
		G::dbl(t2, t1); // 8x
		G::add(t1, t1, t2); // 12x
		G::add(z, t1, x);
		break;
	}
	case 14: {
		G t;
		// (8 - 1) * 2
		G::dbl(t, x);
		G::dbl(t, t);
		G::dbl(t, t);
		G::sub(t, t, x);
		G::dbl(z, t);
		break;
	}
	case 15: {
		G t;
		G::dbl(t, x);
		G::dbl(t, t);
		G::dbl(t, t);
		G::dbl(t, t);
		G::sub(z, t, x);
		break;
	}
	case 16: {
		G::dbl(z, x);
		G::dbl(z, z);
		G::dbl(z, z);
		G::dbl(z, z);
		break;
	}
	default:
		return false;
	}
	if (isNegative) {
		G::neg(z, z);
	}
	return true;
}

} // mcl::ec

/*
	elliptic curve
	y^2 = x^3 + ax + b (affine)
	y^2 = x^3 + az^4 + bz^6 (Jacobi) x = X/Z^2, y = Y/Z^3
*/
template<class _Fp>
class EcT : public fp::Serializable<EcT<_Fp> > {
public:
	typedef _Fp Fp; // definition field
	typedef _Fp BaseFp;
	Fp x, y, z;
	static int mode_;
	static Fp a_;
	static Fp b_;
	static Fp b3_;
	static int specialA_;
	static int specialB_;
	static int ioMode_;
	/*
		order_ is the order of G2 which is the subgroup of EcT<Fp2>.
		check the order of the elements if verifyOrder_ is true
	*/
	static bool verifyOrder_;
	static mpz_class order_;
	static bool (*mulVecGLV)(EcT& z, const EcT *xVec, const void *yVec, size_t n, bool constTime, size_t b);
	static void (*mulVecOpti)(EcT& z, EcT *xVec, const Fr *yVec, size_t n, size_t b);
	static void (*mulEachOpti)(EcT *xVec, const Fr *yVec, size_t n);
	static bool (*isValidOrderFast)(const EcT& x);
	/* default constructor is undefined value */
	EcT() {}
	EcT(const Fp& _x, const Fp& _y)
	{
		set(_x, _y);
	}
	bool isNormalized() const
	{
		return isZero() || z.isOne();
	}
private:
	bool isValidAffine() const
	{
		return ec::isValidAffine(*this);
	}
public:
	void normalize()
	{
		switch (mode_) {
		case ec::Jacobi:
			ec::normalizeJacobi(*this);
			break;
		case ec::Proj:
			ec::normalizeProj(*this);
			break;
		}
	}
	static void normalize(EcT& y, const EcT& x)
	{
		y = x;
		y.normalize();
	}
	static void normalizeVec(EcT *y, const EcT *x, size_t n)
	{
		if (mode_ == ec::Affine) {
			if (y == x) return;
			for (size_t i = 0; i < n; i++) {
				y[i] = x[i];
			}
			return;
		}
		ec::normalizeVec(y, x, n);
	}
	static inline void init(const Fp& a, const Fp& b, int mode = ec::Jacobi)
	{
		a_ = a;
		b_ = b;
		b3_ = b * 3;
		if (a_.isZero()) {
			specialA_ = ec::local::Zero;
		} else if (a_ == -3) {
			specialA_ = ec::local::Minus3;
		} else {
			specialA_ = ec::local::GenericA;
		}
		if (b_ == 1) {
			specialB_ = ec::local::Plus1;
		} else
		if (b_ == 4) {
			specialB_ = ec::local::Plus4;
		} else
		{
			specialB_ = ec::local::GenericB;
		}
		ioMode_ = 0;
		verifyOrder_ = false;
		order_ = 0;
		mulVecGLV = 0;
		mulVecOpti = 0;
		mulEachOpti = 0;
		isValidOrderFast = 0;
		mode_ = mode;
	}
	static inline int getMode() { return mode_; }
	/*
		verify the order of *this is equal to order if order != 0
		in constructor, set, setStr, operator<<().
	*/
	static void setOrder(const mpz_class& order)
	{
		if (order != 0) {
			verifyOrder_ = true;
			order_ = order;
		} else {
			verifyOrder_ = false;
			// don't clear order_ because it is used for isValidOrder()
		}
	}
	static void setVerifyOrderFunc(bool f(const EcT&))
	{
		isValidOrderFast = f;
	}
	static void setMulVecGLV(bool f(EcT& z, const EcT *xVec, const void *yVec, size_t yn, bool constTime, size_t b))
	{
		mulVecGLV = f;
	}
	static void setMulVecOpti(void f(EcT& z, EcT *xVec, const Fr *yVec, size_t yn, size_t b))
	{
		mulVecOpti = f;
	}
	static void setMulEachOpti(void f(EcT *xVec, const Fr *yVec, size_t yn))
	{
		mulEachOpti = f;
	}
	static inline void init(bool *pb, const char *astr, const char *bstr, int mode = ec::Jacobi)
	{
		Fp a, b;
		a.setStr(pb, astr);
		if (!*pb) return;
		b.setStr(pb, bstr);
		if (!*pb) return;
		init(a, b, mode);
	}
	// verify the order
	bool isValidOrder() const
	{
		if (isValidOrderFast) {
			return isValidOrderFast(*this);
		}
		EcT Q;
		EcT::mulGeneric(Q, *this, order_);
		return Q.isZero();
	}
	bool isValid() const
	{
		if (z.isZero()) return true;
		if (z.isOne()) {
			if (!isValidAffine()) return false;
		} else if (mode_ == ec::Jacobi) {
			if (!ec::isValidJacobi(*this)) return false;
		} else if (mode_ == ec::Proj) {
			if (!ec::isValidProj(*this)) return false;
		} else if (mode_ == ec::Affine) {
			return false;
		}
		if (verifyOrder_) return isValidOrder();
		return true;
	}
	void set(bool *pb, const Fp& x, const Fp& y, bool verify = true)
	{
		this->x = x;
		this->y = y;
		z = 1;
		if (!verify || (isValidAffine() && (!verifyOrder_ || isValidOrder()))) {
			*pb = true;
			return;
		}
		*pb = false;
		clear();
	}
	void clear()
	{
		if (mode_ == ec::Jacobi) {
			x = 0;
			y = 0;
			z.clear();
		} else { // ec::Proj
			x.clear();
			y = 1;
			z.clear();
		}
	}
	static inline void clear(EcT& P)
	{
		P.clear();
	}
	static inline void dbl(EcT& R, const EcT& P)
	{
		switch (mode_) {
		case ec::Jacobi:
			ec::dblJacobi(R, P);
			break;
		case ec::Proj:
			ec::dblProj(R, P);
			break;
		case ec::Affine:
			ec::dblAffine(R, P);
			break;
		}
	}
	static inline void add(EcT& R, const EcT& P, const EcT& Q)
	{
		switch (mode_) {
		case ec::Jacobi:
			ec::addJacobi(R, P, Q);
			break;
		case ec::Proj:
			ec::addProj(R, P, Q);
			break;
		case ec::Affine:
			ec::addAffine(R, P, Q);
			break;
		}
	}
	static inline void sub(EcT& R, const EcT& P, const EcT& Q)
	{
		EcT nQ;
		neg(nQ, Q);
		add(R, P, nQ);
	}
	static inline void neg(EcT& R, const EcT& P)
	{
		if (P.isZero()) {
			R.clear();
			return;
		}
		R.x = P.x;
		Fp::neg(R.y, P.y);
		R.z = P.z;
	}
	static inline void mul(EcT& z, const EcT& x, const Fr& y, bool constTime = false)
	{
		if (mulVecGLV) {
			mulVecGLV(z, &x, &y, 1, constTime, 0);
			return;
		}
		fp::Block b;
		y.getBlock(b);
		mulArray(z, x, b.p, b.n, false, constTime);
	}
	static inline void mul(EcT& z, const EcT& x, int64_t y)
	{
		const uint64_t u = fp::abs_(y);
#if MCL_SIZEOF_UNIT == 8
		const uint64_t *ua = &u;
		const size_t un = 1;
#else
		uint32_t ua[2] = { uint32_t(u), uint32_t(u >> 32) };
		const size_t un = ua[1] ? 2 : 1;
#endif
		mulArray(z, x, ua, un, y < 0);
	}
	static inline void mul(EcT& z, const EcT& x, const mpz_class& y)
	{
		mulArray(z, x, gmp::getUnit(y), gmp::getUnitSize(y), y < 0);
	}
	// not const time
	static inline void mulCT(EcT& z, const EcT& x, const Fr& y)
	{
		mul(z, x, y, true);
	}
	static inline void mulCT(EcT& z, const EcT& x, const mpz_class& y)
	{
		mulArray(z, x, gmp::getUnit(y), gmp::getUnitSize(y), y < 0, true);
	}
	/*
		0 <= P for any P
		(Px, Py) <= (P'x, P'y) iff Px < P'x or Px == P'x and Py <= P'y
		@note compare function calls normalize()
	*/
	template<class F>
	static inline int compareFunc(const EcT& P_, const EcT& Q_, F comp)
	{
		const bool QisZero = Q_.isZero();
		if (P_.isZero()) {
			if (QisZero) return 0;
			return -1;
		}
		if (QisZero) return 1;
		EcT P(P_), Q(Q_);
		P.normalize();
		Q.normalize();
		int c = comp(P.x, Q.x);
		if (c > 0) return 1;
		if (c < 0) return -1;
		return comp(P.y, Q.y);
	}
	static inline int compare(const EcT& P, const EcT& Q)
	{
		return compareFunc(P, Q, Fp::compare);
	}
	static inline int compareRaw(const EcT& P, const EcT& Q)
	{
		return compareFunc(P, Q, Fp::compareRaw);
	}
	bool isZero() const
	{
		return z.isZero();
	}
	static inline bool isMSBserialize()
	{
		return !b_.isZero() && (Fp::BaseFp::getBitSize() & 7) != 0;
	}
	// return serialized byte size
	static inline size_t getSerializedByteSize()
	{
		const size_t n = Fp::getByteSize();
		const size_t adj = isMSBserialize() ? 0 : 1;
		return n + adj;
	}
	template<class OutputStream>
	void save(bool *pb, OutputStream& os, int ioMode) const
	{
		const char sep = *fp::getIoSeparator(ioMode);
		if (ioMode & IoEcProj) {
			cybozu::writeChar(pb, os, '4'); if (!*pb) return;
			if (sep) {
				cybozu::writeChar(pb, os, sep);
				if (!*pb) return;
			}
			x.save(pb, os, ioMode); if (!*pb) return;
			if (sep) {
				cybozu::writeChar(pb, os, sep);
				if (!*pb) return;
			}
			y.save(pb, os, ioMode); if (!*pb) return;
			if (sep) {
				cybozu::writeChar(pb, os, sep);
				if (!*pb) return;
			}
			z.save(pb, os, ioMode);
			return;
		}
		EcT P(*this);
		P.normalize();
		if (ioMode & IoEcAffineSerialize) {
			if (b_ == 0) { // assume Zero if x = y = 0
				*pb = false;
				return;
			}
			if (isZero()) {
				// all zero
				P.z.save(pb, os, IoSerialize);
				if (!*pb) return;
				P.z.save(pb, os, IoSerialize);
				return;
			}
			P.x.save(pb, os, IoSerialize);
			if (!*pb) return;
			P.y.save(pb, os, IoSerialize);
			return;
		}
		if (ioMode & (IoSerialize | IoSerializeHexStr)) {
			const size_t n = Fp::getByteSize();
			const size_t adj = isMSBserialize() ? 0 : 1;
			uint8_t buf[sizeof(Fp) + 1];
			if (Fp::BaseFp::getETHserialization()) {
				const uint8_t c_flag = 0x80;
				const uint8_t b_flag = 0x40;
				const uint8_t a_flag = 0x20;
				if (P.isZero()) {
					buf[0] = c_flag | b_flag;
					memset(buf + 1, 0, n - 1);
				} else {
					cybozu::MemoryOutputStream mos(buf, n);
					P.x.save(pb, mos, IoSerialize); if (!*pb) return;
					uint8_t cba = c_flag;
					if (ec::local::get_a_flag(P.y)) cba |= a_flag;
					buf[0] |= cba;
				}
			} else {
				/*
					if (isMSBserialize()) {
					  // n bytes
					  x | (y.isOdd ? 0x80 : 0)
					} else {
					  // n + 1 bytes
					  (y.isOdd ? 3 : 2), x
					}
				*/
				if (isZero()) {
					memset(buf, 0, n + adj);
				} else {
					cybozu::MemoryOutputStream mos(buf + adj, n);
					P.x.save(pb, mos, IoSerialize); if (!*pb) return;
					if (adj) {
						buf[0] = P.y.isOdd() ? 3 : 2;
					} else {
						if (P.y.isOdd()) {
							buf[n - 1] |= 0x80;
						}
					}
				}
			}
			if (ioMode & IoSerializeHexStr) {
				mcl::fp::writeHexStr(pb, os, buf, n + adj);
			} else {
				cybozu::write(pb, os, buf, n + adj);
			}
			return;
		}
		if (isZero()) {
			cybozu::writeChar(pb, os, '0');
			return;
		}
		if (ioMode & IoEcCompY) {
			cybozu::writeChar(pb, os, P.y.isOdd() ? '3' : '2');
			if (!*pb) return;
			if (sep) {
				cybozu::writeChar(pb, os, sep);
				if (!*pb) return;
			}
			P.x.save(pb, os, ioMode);
		} else {
			cybozu::writeChar(pb, os, '1'); if (!*pb) return;
			if (sep) {
				cybozu::writeChar(pb, os, sep);
				if (!*pb) return;
			}
			P.x.save(pb, os, ioMode); if (!*pb) return;
			if (sep) {
				cybozu::writeChar(pb, os, sep);
				if (!*pb) return;
			}
			P.y.save(pb, os, ioMode);
		}
	}
	template<class InputStream>
	void load(bool *pb, InputStream& is, int ioMode)
	{
		z = 1;
		if (ioMode & IoEcAffineSerialize) {
			if (b_ == 0) { // assume Zero if x = y = 0
				*pb = false;
				return;
			}
			x.load(pb, is, IoSerialize);
			if (!*pb) return;
			y.load(pb, is, IoSerialize);
			if (!*pb) return;
			if (x.isZero() && y.isZero()) {
				z.clear();
				return;
			}
			goto verifyValidAffine;
		}
		if (ioMode & (IoSerialize | IoSerializeHexStr)) {
			const size_t n = Fp::getByteSize();
			const size_t adj = isMSBserialize() ? 0 : 1;
			const size_t n1 = n + adj;
			uint8_t buf[sizeof(Fp) + 1];
			size_t readSize;
			if (ioMode & IoSerializeHexStr) {
				readSize = mcl::fp::readHexStr(buf, n1, is);
			} else {
				readSize = cybozu::readSome(buf, n1, is);
			}
			if (readSize != n1) {
				*pb = false;
				return;
			}
			if (Fp::BaseFp::getETHserialization()) {
				const uint8_t c_flag = 0x80;
				const uint8_t b_flag = 0x40;
				const uint8_t a_flag = 0x20;
				*pb = false;
				if ((buf[0] & c_flag) == 0) { // assume compressed
					return;
				}
				if (buf[0] & b_flag) { // infinity
					if (buf[0] != (c_flag | b_flag)) return;
					for (size_t i = 1; i < n - 1; i++) {
						if (buf[i]) return;
					}
					clear();
					*pb = true;
					return;
				}
				bool a = (buf[0] & a_flag) != 0;
				buf[0] &= ~(c_flag | b_flag | a_flag);
				mcl::fp::local::byteSwap(buf, n);
				x.setArray(pb, buf, n);
				if (!*pb) return;
				getWeierstrass(y, x);
				if (!Fp::squareRoot(y, y)) {
					*pb = false;
					return;
				}
				if (ec::local::get_a_flag(y) ^ a) {
					Fp::neg(y, y);
				}
				goto verifyOrder;
			}
			if (bint::isZeroN(buf, n1)) {
				clear();
				*pb = true;
				return;
			}
			bool isYodd;
			if (adj) {
				char c = buf[0];
				if (c != 2 && c != 3) {
					*pb = false;
					return;
				}
				isYodd = c == 3;
			} else {
				isYodd = (buf[n - 1] >> 7) != 0;
				buf[n - 1] &= 0x7f;
			}
			x.setArray(pb, buf + adj, n);
			if (!*pb) return;
			*pb = getYfromX(y, x, isYodd);
			if (!*pb) return;
		} else {
			char c = 0;
			if (!fp::local::skipSpace(&c, is)) {
				*pb = false;
				return;
			}
			if (c == '0') {
				clear();
				*pb = true;
				return;
			}
			x.load(pb, is, ioMode); if (!*pb) return;
			if (c == '1') {
				y.load(pb, is, ioMode); if (!*pb) return;
				goto verifyValidAffine;
			} else if (c == '2' || c == '3') {
				bool isYodd = c == '3';
				*pb = getYfromX(y, x, isYodd);
				if (!*pb) return;
			} else if (c == '4') {
				y.load(pb, is, ioMode); if (!*pb) return;
				z.load(pb, is, ioMode); if (!*pb) return;
				if (mode_ == ec::Affine) {
					if (!z.isZero() && !z.isOne()) {
						*pb = false;
						return;
					}
				}
				*pb = isValid();
				return;
			} else {
				*pb = false;
				return;
			}
		}
	verifyOrder:
		if (verifyOrder_ && !isValidOrder()) {
			*pb = false;
		} else {
			*pb = true;
		}
		return;
	verifyValidAffine:
		if (!isValidAffine()) {
			*pb = false;
			return;
		}
		goto verifyOrder;
	}
	// deprecated
	static void setCompressedExpression(bool compressedExpression = true)
	{
		if (compressedExpression) {
			ioMode_ |= IoEcCompY;
		} else {
			ioMode_ &= ~IoEcCompY;
		}
	}
	/*
		set IoMode for operator<<(), or operator>>()
	*/
	static void setIoMode(int ioMode)
	{
		assert(!(ioMode & 0xff));
		ioMode_ = ioMode;
	}
	static inline int getIoMode() { return Fp::BaseFp::getIoMode() | ioMode_; }
	static inline void getWeierstrass(Fp& yy, const Fp& x)
	{
		Fp t;
		Fp::sqr(t, x);
		t += a_;
		t *= x;
		Fp::add(yy, t, b_);
	}
	static inline bool getYfromX(Fp& y, const Fp& x, bool isYodd)
	{
		getWeierstrass(y, x);
		if (!Fp::squareRoot(y, y)) {
			return false;
		}
		if (y.isOdd() ^ isYodd) {
			Fp::neg(y, y);
		}
		return true;
	}
	inline friend EcT operator+(const EcT& x, const EcT& y) { EcT z; add(z, x, y); return z; }
	inline friend EcT operator-(const EcT& x, const EcT& y) { EcT z; sub(z, x, y); return z; }
	template<class INT>
	inline friend EcT operator*(const EcT& x, const INT& y) { EcT z; mul(z, x, y); return z; }
	EcT& operator+=(const EcT& x) { add(*this, *this, x); return *this; }
	EcT& operator-=(const EcT& x) { sub(*this, *this, x); return *this; }
	template<class INT>
	EcT& operator*=(const INT& x) { mul(*this, *this, x); return *this; }
	EcT operator-() const { EcT x; neg(x, *this); return x; }
	bool operator==(const EcT& rhs) const
	{
		switch (mode_) {
		case ec::Jacobi:
			return ec::isEqualJacobi(*this, rhs);
		case ec::Proj:
			return ec::isEqualProj(*this, rhs);
		case ec::Affine:
		default:
			return x == rhs.x && y == rhs.y && z == rhs.z;
		}
	}
	// return (==rhs) ? 1 : (==-rhs) ? -1 : 0
	int isEqualOrMinus(const EcT& rhs) const
	{
		switch (mode_) {
		case ec::Jacobi:
			return ec::isEqualOrMinusJacobi(*this, rhs);
		case ec::Proj:
			return ec::isEqualOrMinusProj(*this, rhs);
		case ec::Affine:
		default:
			if (x == rhs.x && z == rhs.z) {
				if (y == rhs.y) return 1;
				if (y == -rhs.y) return -1;
			}
			return 0;
		}
	}
	bool operator!=(const EcT& rhs) const { return !operator==(rhs); }
	bool operator<(const EcT& rhs) const
	{
		return compare(*this, rhs) < 0;
	}
	bool operator>=(const EcT& rhs) const { return !operator<(rhs); }
	bool operator>(const EcT& rhs) const { return rhs < *this; }
	bool operator<=(const EcT& rhs) const { return !operator>(rhs); }
	static inline void mulArrayCT(EcT& z, const EcT& x, const Unit *y, size_t yn, bool isNegative)
	{
		const int w = 4; // don't change
		const size_t tblSize = 1u << w;
		const size_t mask = tblSize - 1;
		const size_t m = sizeof(Unit) * 8 / w;
		EcT tbl[tblSize];
		tbl[0].clear();
		tbl[1] = x;
		for (size_t i = 2; i < tblSize; i++) {
			add(tbl[i], tbl[i - 1], x);
		}
		z.clear();
		for (size_t i = 0; i < yn; i++) {
			Unit v = y[yn - 1 - i];
			for (size_t j = 0; j < m; j++) {
				for (size_t k = 0; k < w; k++) {
					EcT::dbl(z, z);
				}
				z += tbl[(v >> ((m - 1 - j) * w)) & mask];
			}
		}
		if (isNegative) {
			EcT::neg(z, z);
		}
	}

	static inline void mulArray(EcT& z, const EcT& x, const Unit *y, size_t yn, bool isNegative = false, bool constTime = false)
	{
		if (constTime) {
			mulArrayCT(z, x, y, yn, isNegative);
			return;
		}
		if (yn == 0) {
			z.clear();
			return;
		}
		yn = bint::getRealSize(y, yn);
		if (yn <= 1 && mcl::ec::mulSmallInt(z, x, *y, isNegative)) return;
		mpz_class v;
		bool b;
		gmp::setArray(&b, v, y, yn);
		assert(b); (void)b;
		if (isNegative) v = -v;
		const int maxW = 5;
		const int maxTblSize = 1 << (maxW - 2);
		/*
			L = log2(y), w = (L <= 32) ? 3 : (L <= 128) ? 4 : 5;
		*/
		const int w = (yn == 1 && *y <= (1ull << 32)) ? 3 : (yn * sizeof(Unit) > 16) ? 5 : 4;
		const size_t tblSize = size_t(1) << (w - 2);
		typedef mcl::FixedArray<int8_t, sizeof(EcT::Fp) * 8 + 1> NafArray;
		NafArray naf;
		EcT tbl[maxTblSize];
		gmp::getNAFwidth(&b, naf, v, w);
		assert(b); (void)b;
		EcT P2;
		dbl(P2, x);
		tbl[0] = x;
		for (size_t i = 1; i < tblSize; i++) {
			add(tbl[i], tbl[i - 1], P2);
		}
		z.clear();
		for (size_t i = 0; i < naf.size(); i++) {
			EcT::dbl(z, z);
			ec::local::addTbl(z, tbl, naf, naf.size() - 1 - i);
		}
	}
	static inline bool mulSmallInt(EcT& z, const EcT& x, Unit y, bool isNegative)
	{
		return mcl::ec::mulSmallInt(z, x, y, isNegative);
	}
	/*
		generic mul
		GLV can't be applied in Fp12 - GT
	*/
	static inline void mulGeneric(EcT& z, const EcT& x, const mpz_class& y)
	{
		mulArray(z, x, gmp::getUnit(y), gmp::getUnitSize(y), y < 0);
	}
	/*
		z = sum_{i=0}^{n-1} xVec[i] * yVec[i]
		return min(N, n)
		@note &z != xVec[i]
	*/
private:
	static inline size_t mulVecN(EcT& z, const EcT *xVec, const Fr *yVec, size_t n)
	{
		const size_t N = mcl::fp::maxMulVecN;
		if (n > N) n = N;
		const int w = 5;
		const size_t tblSize = 1 << (w - 2);
		typedef mcl::FixedArray<int8_t, sizeof(EcT::Fp) * 8 + 1> NafArray;
		NafArray naf[N];
		EcT tbl[N][tblSize];
		size_t maxBit = 0;
		mpz_class y;
		for (size_t i = 0; i < n; i++) {
			bool b;
			yVec[i].getMpz(&b, y);
			assert(b); (void)b;
			gmp::getNAFwidth(&b, naf[i], y, w);
			assert(b); (void)b;
			if (naf[i].size() > maxBit) maxBit = naf[i].size();
			EcT P2;
			EcT::dbl(P2, xVec[i]);
			tbl[i][0] = xVec[i];
			for (size_t j = 1; j < tblSize; j++) {
				EcT::add(tbl[i][j], tbl[i][j - 1], P2);
			}
		}
		z.clear();
		EcT::normalizeVec(&tbl[0][0], &tbl[0][0], n * tblSize);
		for (size_t i = 0; i < maxBit; i++) {
			EcT::dbl(z, z);
			for (size_t j = 0; j < n; j++) {
				ec::local::addTbl(z, tbl[j], naf[j], maxBit - 1 - i);
			}
		}
		return n;
	}

public:
	/*
		estimation for n multVec
		GLV method x n-times
		L : bitsize (=256 for Fr)
		w : withdow size (=5 for L=256)
		S : splitSize (=2 for G1, =4 for G2)
		#DBL = L/S, #ADD = ((2^(w-2) + (L/(Sw)S)) * n
		mulVecLong
		c = 5 (for n <= 256), c = 6 for n = 512
		#DBL = L, #ADD = (n + 2^(c+1)-1)*(L/c)

		#ADD
		n = 128, 256, 512
		GLV : 7680, 15360, 30720
		Long: 9779, 16322, 24533
	*/
	static inline void mulVec(EcT& z, EcT *xVec, const Fr *yVec, size_t n, size_t b = 0)
	{
		if (n == 0) {
			z.clear();
			return;
		}
		if (mulVecOpti && n >= 128) {
			mulVecOpti(z, xVec, yVec, n, b);
			return;
		}
		if (mulVecGLV && mulVecGLV(z, xVec, yVec, n, false, b)) {
			return;
		}
		EcT r;
		r.clear();
		while (n > 0) {
			EcT t;
			size_t done = mulVecN(t, xVec, yVec, n);
			r += t;
			xVec += done;
			yVec += done;
			n -= done;
		}
		z = r;
	}
	// multi thread version of mulVec
	// the num of thread is automatically detected if cpuN = 0
	static inline void mulVecMT(EcT& z, EcT *xVec, const Fr *yVec, size_t n, size_t cpuN = 0)
	{
#ifdef MCL_USE_OMP
	const size_t minN = mcl::fp::maxMulVecN;
	if (cpuN == 0) {
		cpuN = omp_get_num_procs();
		if (n < minN * cpuN) {
			cpuN = (n + minN - 1) / minN;
		}
	}
	if (cpuN <= 1 || n <= cpuN) {
		mulVec(z, xVec, yVec, n);
		return;
	}
	EcT *zs = (EcT*)CYBOZU_ALLOCA(sizeof(EcT) * cpuN);
	size_t q = n / cpuN;
	size_t r = n % cpuN;
	#pragma omp parallel for
	for (size_t i = 0; i < cpuN; i++) {
		size_t adj = q * i + fp::min_(i, r);
		mulVec(zs[i], xVec + adj, yVec + adj, q + (i < r));
	}
	z.clear();
//	#pragma omp declare reduction(red:EcT:omp_out *= omp_in) initializer(omp_priv = omp_orig)
//	#pragma omp parallel for reduction(red:z)
	for (size_t i = 0; i < cpuN; i++) {
		z += zs[i];
	}
#else
		(void)cpuN;
		mulVec(z, xVec, yVec, n);
#endif
	}
	// xVec[i] *= yVec[i]
	static void mulEach(EcT *xVec, const Fr *yVec, size_t n)
	{
		if (mulEachOpti && n >= 16) {
			size_t n16 = n & ~size_t(16-1);
			mulEachOpti(xVec, yVec, n16);
			xVec += n16;
			yVec += n16;
			n -= n16;
		}
		for (size_t i = 0; i < n; i++) {
			xVec[i] *= yVec[i];
		}
	}
#ifndef CYBOZU_DONT_USE_EXCEPTION
	static inline void init(const std::string& astr, const std::string& bstr, int mode = ec::Jacobi)
	{
		bool b;
		init(&b, astr.c_str(), bstr.c_str(), mode);
		if (!b) throw cybozu::Exception("mcl:EcT:init");
	}
	void set(const Fp& _x, const Fp& _y, bool verify = true)
	{
		bool b;
		set(&b, _x, _y, verify);
		if (!b) throw cybozu::Exception("ec:EcT:set") << _x << _y;
	}
	template<class OutputStream>
	void save(OutputStream& os, int ioMode = IoSerialize) const
	{
		bool b;
		save(&b, os, ioMode);
		if (!b) throw cybozu::Exception("EcT:save");
	}
	template<class InputStream>
	void load(InputStream& is, int ioMode = IoSerialize)
	{
		bool b;
		load(&b, is, ioMode);
		if (!b) throw cybozu::Exception("EcT:load");
	}
#endif
#ifndef CYBOZU_DONT_USE_STRING
	// backward compatilibity
	static inline void setParam(const std::string& astr, const std::string& bstr, int mode = ec::Jacobi)
	{
		init(astr, bstr, mode);
	}
	friend inline std::istream& operator>>(std::istream& is, EcT& self)
	{
		self.load(is, fp::detectIoMode(getIoMode(), is));
		return is;
	}
	friend inline std::ostream& operator<<(std::ostream& os, const EcT& self)
	{
		self.save(os, fp::detectIoMode(getIoMode(), os));
		return os;
	}
#endif
};

template<class Fp> Fp EcT<Fp>::a_;
template<class Fp> Fp EcT<Fp>::b_;
template<class Fp> Fp EcT<Fp>::b3_;
template<class Fp> int EcT<Fp>::specialA_;
template<class Fp> int EcT<Fp>::specialB_;
template<class Fp> int EcT<Fp>::ioMode_;
template<class Fp> bool EcT<Fp>::verifyOrder_;
template<class Fp> mpz_class EcT<Fp>::order_;
template<class Fp> bool (*EcT<Fp>::mulVecGLV)(EcT& z, const EcT *xVec, const void *yVec, size_t n, bool constTime, size_t b);
template<class Fp> void (*EcT<Fp>::mulVecOpti)(EcT& z, EcT *xVec, const Fr *yVec, size_t n, size_t b);
template<class Fp> bool (*EcT<Fp>::isValidOrderFast)(const EcT& x);
template<class Fp> int EcT<Fp>::mode_;
template<class Fp> void (*EcT<Fp>::mulEachOpti)(EcT<Fp> *xVec, const Fr *yVec, size_t n);

void initForSecp256k1(); // implemented in fp.cpp
/*
	Ec : elliptic curve
	Zn : cyclic group of the order |Ec|
	set P the generator of Ec if P != 0
*/
template<class Ec>
void initCurve(bool *pb, int curveType, Ec *P = 0, mcl::ec::Mode ecMode = ec::Jacobi)
{
	typedef typename Ec::Fp Fp;
	*pb = false;
	const EcParam *ecParam = getEcParam(curveType);
	if (ecParam == 0) return;

	Fr::init(pb, ecParam->n);
	if (!*pb) return;
	Fp::init(pb, ecParam->p);
	if (!*pb) return;
	Ec::init(pb, ecParam->a, ecParam->b, ecMode);
	if (!*pb) return;
	if (P) {
		Fp x, y;
		x.setStr(pb, ecParam->gx);
		if (!*pb) return;
		y.setStr(pb, ecParam->gy);
		if (!*pb) return;
		P->set(pb, x, y);
		if (!*pb) return;
	}
	if (curveType == MCL_SECP256K1) {
		initForSecp256k1();
	} else {
		Ec::setMulVecGLV(0);
	}
}

#ifndef CYBOZU_DONT_USE_EXCEPTION
template<class Ec>
void initCurve(int curveType, Ec *P = 0, mcl::ec::Mode ecMode = ec::Jacobi)
{
	bool b;
	initCurve<Ec>(&b, curveType, P, ecMode);
	if (!b) throw cybozu::Exception("mcl:initCurve") << curveType << ecMode;
}
#endif

} // mcl

#ifndef CYBOZU_DONT_USE_EXCEPTION
#ifdef CYBOZU_USE_BOOST
namespace mcl {
template<class Fp>
size_t hash_value(const mcl::EcT<Fp>& P_)
{
	if (P_.isZero()) return 0;
	mcl::EcT<Fp> P(P_); P.normalize();
	return mcl::hash_value(P.y, mcl::hash_value(P.x));
}

}
#else
namespace std { CYBOZU_NAMESPACE_TR1_BEGIN

template<class Fp>
struct hash<mcl::EcT<Fp> > {
	size_t operator()(const mcl::EcT<Fp>& P_) const
	{
		if (P_.isZero()) return 0;
		mcl::EcT<Fp> P(P_); P.normalize();
		return hash<Fp>()(P.y, hash<Fp>()(P.x));
	}
};

CYBOZU_NAMESPACE_TR1_END } // std
#endif
#endif

#ifdef _MSC_VER
	#pragma warning(pop)
#endif
