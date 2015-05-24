#pragma once
#include<cstdlib>
#include<cassert>

#include <omp.h>

#include "v2r8.h"
#include "v4r8.h"

struct Gravity{
	enum{
		NIMAX = 1024,
		MAXTHREAD = 16,
		NACT_PARALLEL_THRESH = 4,
	};

	struct GParticle{
		v2r8 pos[3];
		v2r8 mass;
		v2r8 vel[3];
		v2r8 tlast;
		v2r8 acc[3];
		v2r8 jrk[3]; // 14 XMM
	};

	struct GPredictor{
		v2r8 pos[3];
		v2r8 mass;
		v2r8 vel[3];
	};

	struct GForce{
		v2r8 ax, ay, az;
		v2r8 jx, jy, jz;

		void clear(){
			ax = ay = az = v2r8(0.0);
			jx = jy = jz = v2r8(0.0);
		}

		void save(
				const v2r8 _ax, const v2r8 _ay, const v2r8 _az,
				const v2r8 _jx, const v2r8 _jy, const v2r8 _jz)
		{
			ax = _ax; ay = _ay; az = _az;
			jx = _jx; jy = _jy; jz = _jz;
		}
		void accumulate(const GForce rhs){
			ax += rhs.ax;
			ay += rhs.ay;
			az += rhs.az;
			jx += rhs.jx;
			jy += rhs.jy;
			jz += rhs.jz;
		}

		void store_2_forces(Force * __restrict fout){
			ax.storel(&fout[0].acc.x);
			ay.storel(&fout[0].acc.y);
			az.storel(&fout[0].acc.z);

			jx.storel(&fout[0].jrk.x);
			jy.storel(&fout[0].jrk.y);
			jz.storel(&fout[0].jrk.z);

			ax.storeh(&fout[1].acc.x);
			ay.storeh(&fout[1].acc.y);
			az.storeh(&fout[1].acc.z);

			jx.storeh(&fout[1].jrk.x);
			jy.storeh(&fout[1].jrk.y);
			jz.storeh(&fout[1].jrk.z);
		}

	};

	const int  nbody;
	GParticle  *ptcl;
	GPredictor *pred;

	Gravity(const int _nbody) : nbody(_nbody) {
		ptcl = allocate<GParticle,  128> (nbody);
		pred = allocate<GPredictor, 128> (nbody);
#pragma omp parallel
		assert(MAXTHREAD >= omp_get_thread_num());
	}
	~Gravity(){
		free(ptcl);
		free(pred);
	}

	static void set_jp_rp(
			const int addr,
			const Particle * __restrict p,
			GParticle      * __restrict ptcl)
	{
		const unsigned ah = unsigned(addr)/2;
		const unsigned al = unsigned(addr)%2;
#if 0
		if(0 == al){
			char *achar = (char *)(ptcl + ah);
			__builtin_prefetch(achar);
			__builtin_prefetch(achar+128);
			__builtin_prefetch(achar+256);
		}
#endif

		const dvec3  pos   = p->pos;
		const double mass  = p->mass;
		const dvec3  vel   = p->vel;
		const double tlast = p->tlast;
		const dvec3  acc   = p->acc;
		const dvec3  jrk   = p->jrk;

		double *dst = (double *)(ptcl + ah) + al;
		dst[ 0] = pos.x;
		dst[ 2] = pos.y;
		dst[ 4] = pos.z;
		dst[ 6] = mass;
		dst[ 8] = vel.x;
		dst[10] = vel.y;
		dst[12] = vel.z;
		dst[14] = tlast;
		dst[16] = acc.x;
		dst[18] = acc.y;
		dst[20] = acc.z;
		dst[22] = jrk.x;
		dst[24] = jrk.y;
		dst[26] = jrk.z;
	}
	void set_jp(const int addr, const Particle &p){
#if 0
		const unsigned ah = unsigned(addr)/2;
		const unsigned al = unsigned(addr)%2;

		const dvec3  pos   = p.pos;
		const double mass  = p.mass;
		const dvec3  vel   = p.vel;
		const double tlast = p.tlast;
		const dvec3  acc   = p.acc;
		const dvec3  jrk   = p.jrk;

		double *dst = (double *)(ptcl + ah) + al;
		dst[ 0] = pos.x;
		dst[ 2] = pos.y;
		dst[ 4] = pos.z;
		dst[ 6] = mass;
		dst[ 8] = vel.x;
		dst[10] = vel.y;
		dst[12] = vel.z;
		dst[14] = tlast;
		dst[16] = acc.x;
		dst[18] = acc.y;
		dst[20] = acc.z;
		dst[22] = jrk.x;
		dst[24] = jrk.y;
		dst[26] = jrk.z;
#else
		set_jp_rp(addr, &p, ptcl);
#endif
	}

	static void predict_all_rp(
			const int nbody, 
			const double tsys, 
			const GParticle * __restrict ptcl,
			GPredictor      * __restrict pred);
	static void predict_all_rp_fast_omp(
			const int nbody, 
			const double tsys, 
			const GParticle * __restrict ptcl,
			GPredictor      * __restrict pred);

	void predict_all(const double tsys){
		predict_all_rp(nbody, tsys, ptcl, pred);
	}
	void predict_all_fast_omp(const double tsys){
		predict_all_rp_fast_omp(nbody, tsys, ptcl, pred);
	}

	void calc_force_in_range(
			const int    is,
			const int    ie,
			const double deps2,
			Force        force[] );
	void calc_force_in_range_fast_omp(
			const int    is,
			const int    ie,
			const double deps2,
			Force        force[] );

	void calc_force_on_first_nact(
			const int    nact,
			const double eps2,
			Force        force[] )
	{
		for(int ii=0; ii<nact; ii+=NIMAX){
			const int ni = (nact-ii) < NIMAX ? (nact-ii) : NIMAX;
			calc_force_in_range(ii, ii+ni, eps2, force);
		}
	}
	void calc_force_on_first_nact_fast_omp(
			const int    nact,
			const double eps2,
			Force        force[] )
	{
		if(nact < NIMAX){
			calc_force_in_range_fast_omp(0, nact, eps2, force);
		}else{
			for(int ii=0; ii<nact; ii+=NIMAX){
				const int ni = (nact-ii) < NIMAX ? (nact-ii) : NIMAX;
				calc_force_in_range_fast_omp(ii, ii+ni, eps2, force);
			}
		}
	}

	static void calc_potential_rp(
			const int    nbody,
			const double deps2,
			const GParticle * __restrict ptcl,
			v4r8            * __restrict xmbuf,
			double          * __restrict potbuf);
	void calc_potential(
			const double deps2,
			double * __restrict potbuf)
	{
		v4r8 *xmbuf = allocate<v4r8, 128>(nbody+1);
		calc_potential_rp(nbody, deps2, ptcl, xmbuf, potbuf);
		free(xmbuf);
	}
};
