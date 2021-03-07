/*
 *  filters.c
 *
 *  Created: 3/17/2011 8:27:17 AM
 *      Author: waltj
 */ 

// #include "misc/debug.h"
#include "filters.h"

//_____ M A C R O S ________________________________________________________

//_____ D E F I N I T I O N S ______________________________________________

//_____ G L O B A L S ______________________________________________________

//_____ L O C A L   P R O T O T Y P E S ____________________________________

void integrateDeltaThetaVelBortz(ixVector3 theta, ixVector3 vel, imus_t *imu, imus_t *imuLast, float Nsteps, float dti);
float deltaThetaDeltaVelRiemannSum( preintegrated_imu_t *output, imu_t *imu, imu_t *imuLast );
float deltaThetaDeltaVelTrapezoidal( preintegrated_imu_t *output, imu_t *imu, imu_t *imuLast );
float deltaThetaDeltaVelBortz( preintegrated_imu_t *output, imu_t *imu, imu_t *imuLast, int Nsteps );
#if 0
float integrateDeltaThetaVelRoscoe(
	preintegrated_imu_t *output, 
	imu_t *imu, 
	imu_t *imuLast, 
	ixVector3 alpha_last, 
	ixVector3 veloc_last, 
	ixVector3 delta_alpha_last, 
	ixVector3 delta_veloc_last);
#endif

//_____ F U N C T I O N S __________________________________________________

void init_iir_filter(iif_filter_t *f)
{
	int gama;
	int alpha;
	float TsFc;

	if(f->opt.n_channels > MAX_NUMBER_IIR_CHANNELS)
	{
// 		dg_printf("IIR channels exceeded max number: %d.  Consider increasing max number", MAX_NUMBER_IIR_CHANNELS);
// 		exit(1);
	}

	f->opt.bit_shift = (ACCUM_WORD_NBITS-1-f->opt.sig_word_nbits)/2;
	
	// Gama = adc_to_iir = alpha + beta
	gama				= 1<<f->opt.bit_shift;
	TsFc				= f->opt.Fc/f->opt.Fs;
	alpha				= (int)( TsFc/(1.0 + TsFc) * (float)gama );
	f->opt.beta			= gama - alpha;
	// alpha_x allows us to combine the adc_to_iir and alpha multiplies into one step
	f->opt.alpha_x		= alpha<<f->opt.bit_shift;
	f->opt.iir_to_adc	= 1.0f/(float)gama;
}


void iir_filter_u16(iif_filter_t *f, unsigned short input[], float output[])
{
	unsigned int i,j;

	for (j=0; j<f->opt.input_size; j+=f->opt.n_channels)
	{
		for (i=0; i<f->opt.n_channels; i++)
		{
			f->accum[i] = f->opt.beta*f->accum[i] + f->opt.alpha_x*((int)input[j+i]);
			f->accum[i] >>= f->opt.bit_shift;
		}
	}

	for (i=0; i<f->opt.n_channels; i++)
		output[i] = f->opt.iir_to_adc*(float)f->accum[i];
}

void iir_filter_s16(iif_filter_t *f, short input[], float output[])
{
	unsigned int i,j;	
	
	for (j=0; j<f->opt.input_size; j+=f->opt.n_channels)
	{
		for (i=0; i<f->opt.n_channels; i++)
		{
			f->accum[i] = f->opt.beta*f->accum[i] + f->opt.alpha_x*((int)input[j+i]);
			f->accum[i] >>= f->opt.bit_shift;
		}
	}
	
	for (i=0; i<f->opt.n_channels; i++)
		output[i] = f->opt.iir_to_adc*(float)f->accum[i];
}







/** 
 * \brief Running Average Filter
 *  A running average of the input array is collected in the mean array.  Filter
 *  is reset when sampleCount equals 0.
 *
 * \param input         Floating point value to be included in the average.
 * \param mean          Average of input
 * \param arraySize     Array length of mean and input arrays.
 * \param sampleCount   Sample number of input.  0 causes filter to be reset.
 */
void running_mean_filter( float input[], float mean[], int arraySize, int sampleCount )
{
    int i;
    float alpha;
    
    if( sampleCount == 0 )
        alpha = 1.0f;
    else
        alpha = 1.0f / (float)sampleCount;
    
    // Find running average
    for( i=0; i<arraySize; i++ )
        mean[i] = (1.0f-alpha)*mean[i] + (alpha)*input[i];
}


/** 
 * \brief Running Average Filter (double)
 *  A running average of the input array is collected in the mean array.  Filter
 *  is reset when sampleCount equals 0.
 *
 * \param input         Double (float 64) value to be included in the average.
 * \param mean          Average of input
 * \param arraySize     Array length of mean and input arrays.
 * \param sampleCount   Sample number of input.  0 causes filter to be reset.
 */
void running_mean_filter_f64( double mean[], float input[], int arraySize, int sampleCount )
{
    int i;
    double alpha;
    
    if( sampleCount == 0 )
        alpha = 1.0;
    else
        alpha = 1.0 / (double)sampleCount;
    
    // Find running average
    for( i=0; i<arraySize; i++ )
        mean[i] = (1.0-alpha)*mean[i] + (alpha)*(double)input[i];
}

#define INVALID_ACCEL 1.0e-6f
void errorCheckImu3(imu3_t *di)
{
	// Error Checking
	if( di->time != 0.0) 
	{
        // Compare to a small number much smaller than IMU noise sigma
		for (int i = 0; i < 3; i++)
		{
			if (fabs(di->I[i].acc[0]) < INVALID_ACCEL && 
				fabs(di->I[i].acc[1]) < INVALID_ACCEL && 
				fabs(di->I[i].acc[2]) < INVALID_ACCEL)
			{
				di->status &= ~IMU_STATUS_IMU_OK_MASK;
			}
		}
    }
}


void tripleToSingleImu(imu_t *result, const imu3_t *di)
{
	imu_t imu = {};
	imu.time = di->time;
	imu.status = di->status;

	int cnt = 0;

	if ((di->status&IMU_STATUS_IMU1_OK)==IMU_STATUS_IMU1_OK)
	{
		add_Vec3_Vec3(imu.I.pqr, imu.I.pqr, di->I[0].pqr);
		add_Vec3_Vec3(imu.I.acc, imu.I.acc, di->I[0].acc);
		cnt++;
	}
	if ((di->status&IMU_STATUS_IMU2_OK)==IMU_STATUS_IMU2_OK)
	{
		add_Vec3_Vec3(imu.I.pqr, imu.I.pqr, di->I[1].pqr);
		add_Vec3_Vec3(imu.I.acc, imu.I.acc, di->I[1].acc);
		cnt++;
	}
	if ((di->status&IMU_STATUS_IMU3_OK)==IMU_STATUS_IMU3_OK)
	{
		add_Vec3_Vec3(imu.I.pqr, imu.I.pqr, di->I[2].pqr);
		add_Vec3_Vec3(imu.I.acc, imu.I.acc, di->I[2].acc);
		cnt++;
	}

	if (cnt)
	{
		float div = 1/(float)cnt;
		mul_Vec3_X(imu.I.pqr, imu.I.pqr, div);
		mul_Vec3_X(imu.I.acc, imu.I.acc, div);
	}

	*result = imu;
}


void singleToTripleImu(imu3_t *result, imu_t *imu)
{
	result->time = imu->time;
	for (int i=0; i<3; i++)
	{
		cpy_Vec3_Vec3(result->I[i].pqr, imu->I.pqr);
		cpy_Vec3_Vec3(result->I[i].acc, imu->I.acc);
	}
	result->status = imu->status | (IMU_STATUS_IMU1_OK | IMU_STATUS_IMU2_OK | IMU_STATUS_IMU3_OK);
}


int preintegratedImuToIMU(imu_t *imu, const preintegrated_imu_t *pImu)
{
    if (pImu->dt == 0.0f)
    {
        return 0;
    }

	imu->time = pImu->time;
    float divDt = 1.0f / pImu->dt;
	mul_Vec3_X(imu->I.pqr, pImu->theta, divDt);
	mul_Vec3_X(imu->I.acc, pImu->vel, divDt);
    return 1;
}


int imuToPreintegratedImu(preintegrated_imu_t *pImu, const imu_t *imu, float dt)
{
    if (dt == 0.0f)
    {
        return 0;
    }

    pImu->time = imu->time;
    pImu->dt = dt;
    mul_Vec3_X(pImu->theta, imu->I.pqr, dt);
    mul_Vec3_X(pImu->vel, imu->I.acc, dt);
    return 1;
}


#define CON_SCUL_INT_STEPS  4

void integrateImu( preintegrated_imu_t *output, imu_t *imu, imu_t *imuLast )
{
	output->time = imu->time;
	output->status = imu->status;

	//	output->dt += deltaThetaDeltaVelRiemannSum(output, imu, imuLast);
	// 	output->dt += deltaThetaDeltaVelTrapezoidal(output, imu, imuLast);

	// Numerical integration of coning and sculling integrals using Bortz's rotation vector formula
	output->dt += deltaThetaDeltaVelBortz(output, imu, imuLast, CON_SCUL_INT_STEPS);
        	
	//  // Roscoe integral
	// 	static ixVector3 alpha_last = { 0 };
	// 	static ixVector3 veloc_last = { 0 };
	// 	static ixVector3 delta_alpha_last = { 0 };
	// 	static ixVector3 delta_veloc_last = { 0 };
	// 	output->dt += integrateDeltaThetaVelRoscoe(output, imu, imu, alpha_last, veloc_last, delta_alpha_last, delta_veloc_last);
}


float deltaThetaDeltaVelRiemannSum( preintegrated_imu_t *output, imu_t *imu, imu_t *imuLast )
{
	ixVector3 tmp3;
	float dt = (float)(imu->time - imuLast->time);
	
	// Use Riemann Sum integral

	// IMU - Delta Theta
	mul_Vec3_X( tmp3, imu->I.pqr, dt );
	add_Vec3_Vec3( output->theta, output->theta, tmp3 );

	// IMU - Delta Velocity
	mul_Vec3_X( tmp3, imu->I.acc, dt );
	add_Vec3_Vec3( output->vel, output->vel, tmp3 );

	// Update history
	*imuLast = *imu;

	return dt;
}


float deltaThetaDeltaVelTrapezoidal( preintegrated_imu_t *output, imu_t *imu, imu_t *imuLast )
{
	ixVector3 tmp3;
	float dt = (float)(imu->time - imuLast->time);
	
	// Use Trapezoidal integral

	// Delta Theta
	add_Vec3_Vec3( tmp3, imu->I.pqr, imuLast->I.pqr );
	mul_Vec3_X( tmp3, tmp3, dt*0.5f );
	add_Vec3_Vec3( output->theta, output->theta, tmp3 );

	// Delta Velocity
	add_Vec3_Vec3( tmp3, imu->I.acc, imuLast->I.acc );
	mul_Vec3_X( tmp3, tmp3, dt*0.5f );
	add_Vec3_Vec3( output->vel, output->vel, tmp3 );

	// Update history
	*imuLast = *imu;

	return dt;
}


void integrateDeltaThetaVelBortz(ixVector3 theta, ixVector3 vel, imus_t *imu, imus_t *imuLast, float Nsteps, float dti)
{
    ixVector3 wb, ab, deltaW, deltaA, tmp1, tmp2, tmp3, tmp4;
    float Kw, mag_theta2, mag_theta4, Kw0;

    // for jj = 1: Nint
    //     wb = W0 + (jj - 1) / Nint * (W1 - W0);
    //     ab = A0 + (jj - 1) / Nint * (A1 - A0);
    //     % Bortz's formula
    //     phi_dot = wb + 0.5 * cross(phi, wb) + 1 / 12 * cross(phi, cross(phi, wb));
    //     % Savage's formula (faster): approximates phi with gyro integral
    //     % phi_dot = Wi + 0.5 * cross(WintB, wb)
    //     phi = phi + phi_dot * dt / Nint;
    //     dv = dv + (ab + cross(phi, ab)) * dt / Nint;
    // end
	
    sub_Vec3_Vec3(deltaW, imu->pqr, imuLast->pqr);
    sub_Vec3_Vec3(deltaA, imu->acc, imuLast->acc);
    Kw0 = 1.0f / 12.0f;
    for (int jj = 0; jj < Nsteps; jj++) 
	{
		float jjDivNsteps = ((float)jj) / Nsteps;
		
	    // Current angular rate and acceleration vectors (linear interpolation)
	    for (int i = 0; i < 3; i++) 
		{
		    wb[i] = imuLast->pqr[i] + deltaW[i] * jjDivNsteps;
		    ab[i] = imuLast->acc[i] + deltaA[i] * jjDivNsteps;
	    }
	    // Coning and sculling integrals
	    cross_Vec3(tmp1, theta, wb);
	    cross_Vec3(tmp2, theta, tmp1);
	    cross_Vec3(tmp3, theta, ab);
        cross_Vec3(tmp4, theta, tmp3);
        mag_theta2 = dot_Vec3(theta);
        mag_theta4 = mag_theta2 * mag_theta2;
        Kw = Kw0 + mag_theta2 / 720.0f + mag_theta4 / 30240.0f + mag_theta4 * mag_theta2 / 1209600.0f;
        for (int i = 0; i < 3; i++) {
		    theta[i] += (wb[i] + 0.5f * tmp1[i] + Kw * tmp2[i]) * dti;
		    vel[i] += (ab[i] + tmp3[i] + 0.5f * tmp4[i]) * dti;
	    }
    }	
}


/* Direct numerical integration of coning and sculling integrals using Bortz's formula */
float deltaThetaDeltaVelBortz(preintegrated_imu_t *output, imu_t *imu, imu_t *imuLast, int Nsteps)
{
	float dt = (float)(imu->time - imuLast->time);
	float dti = dt / (float)Nsteps;

	// IMU
	integrateDeltaThetaVelBortz(output->theta, output->vel, &(imu->I), &(imuLast->I), (float)Nsteps, (float)dti);

	// Update history
	*imuLast = *imu;

	return dt;
}


#if 0
float integrateDeltaThetaVelRoscoe( 
	preintegrated_imu_t *output, 
	imu_t *imu, 
	imu_t *imuLast, 	
	ixVector3 alpha_last,
	ixVector3 veloc_last,
	ixVector3 delta_alpha_last,
	ixVector3 delta_veloc_last
 )
{
	ixVector3 tmp3;
	float dt = (float)(imu->time - imuLast->time);
		
	// Roscoe (EQ-32) coning integral
	ixVector3 term1;
	ixVector3 term2;
	ixVector3 alpha;
	ixVector3 veloc;
	ixVector3 delta_alpha;
	ixVector3 delta_veloc;

	//__________________________________________________________________________________________________________________
	// Roscoe (EQ-32) coning integral:     [DELTA THETA = sum((1/2)*(alpha_last+(1/6)*delta_alpha_last) >< delta_alpha)]
	mul_Vec3_X(alpha, imu->I.pqr, dt);										//alpha				<-- [pqr] * [dt]
	sub_Vec3_Vec3(delta_alpha, alpha, alpha_last);							//delta_alpha		<-- [alpha] - [alpha_last]
	div_Vec3_X(tmp3, delta_alpha_last, 6.0f);								//tmp3				<-- [delta_alpha_last] * [(1/6)]
	add_Vec3_Vec3(tmp3, alpha_last, tmp3);									//tmp3				<-- [alpha_last] + [(1/6)*delta_alpha_last]
	div_Vec3_X(tmp3, tmp3, 2.0f);											//tmp3   			<-- [(alpha_last+(1/6)*delta_alpha_last)] * [(1/2)]
	cross_Vec3(term1, tmp3, delta_alpha);									//term1				<-- [(1/2)*(alpha_last+(1/6)*delta_alpha_last)]	>< [delta_alpha] 
	add_Vec3_Vec3(output->theta, output->theta, term1);						//theta				<-- sum[(1/2)*(alpha_last+(1/6)*delta_alpha_last)><delta_alpha]
	cpy_Vec3_Vec3(alpha_last, alpha);										//alpha_last		<-- alpha           {age alpha}
	cpy_Vec3_Vec3(delta_alpha_last, delta_alpha);							//delta_alpha_last  <-- delta_alpha     {age delta_alpha}
	//_________________________________________________________________________________________________________________
	// Roscoe (EQ-33) sculling integral:   [DELTA VELOC = sum((1/2)*(alpha_last+(1/6)*delta_alpha_last) >< delta_veloc)
	//                                                 + sum((1/2)*(veloc_last+(1/6)*delta_veloc_last) >< delta_alpha)] 	 
	mul_Vec3_X(veloc, imu->I.acc, dt);										//veloc				<-- [acc] * [dt]
	sub_Vec3_Vec3(delta_veloc, veloc, veloc_last);							//delta_veloc		<-- [veloc] - [veloc_last]
	cross_Vec3(term1, tmp3, delta_veloc);									//term1				<-- [(1/2)*(alpha_last+(1/6)*delta_alpha_last)] >< [delta_veloc]
	div_Vec3_X(tmp3, delta_veloc_last, 6.0f);								//tmp3				<-- [delta_veloc_last] * [(1/6)]
	add_Vec3_Vec3(tmp3, veloc_last, tmp3);									//tmp3				<-- [veloc_last] + [(1/6)*delta_veloc_last]
	div_Vec3_X(tmp3, tmp3, 2.0f);											//tmp3				<-- [(veloc_last+(1/6)*delta_veloc_last)] * [(1/2)]
	cross_Vec3(term2, tmp3, delta_alpha);									//term2				<-- [(1/2)*(veloc_last+(1/6)*delta_veloc_last)] >< [delta_alpha]
	add_Vec3_Vec3(output->uvw, output->uvw, term1);							//uvw    			<-- sum[(1/2)*(alpha_last+(1/6)*delta_alpha_last)><delta_veloc ...
	add_Vec3_Vec3(output->uvw, output->uvw, term2);							//...				...   +[(1/2)*(veloc_last+(1/6)*delta_veloc_last)><delta_alpha]
	cpy_Vec3_Vec3(veloc_last, veloc);										//veloc_last		<-- veloc           {age veloc}
	cpy_Vec3_Vec3(delta_veloc_last, delta_veloc);							//delta_veloc_last  <-- delta_veloc     {age delta_veloc}
	
	// Update history
	*imuLast = *imu;
	
	return dt;
}
#endif

