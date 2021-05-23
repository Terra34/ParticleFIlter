#include <math.h>
#include <float.h>
#include <stdlib.h>
#include "particleFilter.h"


float pow2(float x)
{
	return x*x;
}

int searchSorted(float* arr, float r, int numParticles){
    /*
        arr => Array that needs to be searched
        r => Value to search by
    */
    int L = 0;
    int R = numParticles - 1;
    int m;

    while (R >= L)
    {
        m = (int) ((L+R) / 2);
        if (r > *(arr + m))
        {
            if (r <= *(arr + m + 1) || (m + 1 > numParticles - 1))
                return m+1;
            L = m + 1;
        }
        else // if (r <= *(arr + m))
        {
            if (r > *(arr + m - 1) || (m - 1 < 0))
                return m;
            R = m - 1;
        }
    }
    return -1;
}

float * getPitchRoll(float* meas){
    // meas is acc_data => [accx accy accz]
    static float pitchRoll[2];
    // roll
    pitchRoll[0] = atan2(-1. * *(meas + 1), sqrt(pow2(*meas) + pow2(*(meas + 2)))) * 180 / PI;

    // pitch
    pitchRoll[1] = atan2(*meas, sqrt(pow2(*(meas + 1)) + pow2(*(meas + 2)))) * 180 / PI;

    return pitchRoll;
}

float getYaw(float* fS, float* meas){
    /*  
        fS => filtered state - assumes degrees!
        meas => magnetometer data [magx magy magz]
    */
   float sina, sinb, cosa, cosb, XH, YH;

   sina = sin(*fS * PI / 180);
   sinb = sin(*(fS + 1) * PI / 180);
   cosa = cos(*fS * PI / 180);
   cosb = cos(*(fS + 1) * PI / 180);
   XH = *meas * cosb + *(meas+1) * sinb * sina + *(meas+2) * sinb * cosa;
   YH = *(meas+1) * cosa + *(meas+2) * sina;
   return atan2(-YH, XH) * 180 / PI;
}

void initializeFilter(	float **particles, float **copyParticles, float *_particles, float *_copyParticles, 
						float *weights, int numParticles, float resetWeights){
	/*
		_particles => Needs to be a pointer to an array of length 2*numParticles
		_copyParticles => Needs to be a pointer to an array of length 2*numParticles
		weights => Needs to be a pointer to an array of length numParticles
	*/
	*particles = _particles;
    *copyParticles = _copyParticles;
    for (int i=0; i<numParticles; i++){
        weights[i] = resetWeights;
        _particles[i*2] = (float)rand() / RAND_MAX * 360 - 180;
        _particles[i*2 + 1] = (float)rand() / RAND_MAX * 360 - 180;
    }
}
						
void predictUpdate(	float *particles, float *weights, float *meas, float *pitchRoll,
					float *sum_weights, int numParticles, float predict_std, float t, 
					struct genState *state){
	/*
		particles => Needs to be a pointer to an array of length 2*numParticles
		weights => Needs to be a pointer to an array of length numParticles
		meas => A pointer to acquired measurements => an array of length 9 in [accx, accy, accz, gyrox, gyroy, gryoz, magx, magy, magz] format
		pitchRoll => A pointer to an array that contains pitch and roll calculated from acc data
	*/
	*sum_weights = 0.f;
	for (int i = 0; i < numParticles; i++){
		particles[i*2] += meas[3] * t + generate(state) * predict_std;
		particles[i*2 + 1] += meas[4] * t + generate(state) * predict_std;


		if (particles[i*2] > 180)
			particles[i*2] -= 360;
		else if (particles[i*2] < -180)
			particles[i*2] += 360;

		if (particles[i*2 + 1] > 180)
			particles[i*2 + 1] -= 360;
		else if (particles[i*2 + 1] < -180)
			particles[i*2 + 1] += 360;

		weights[i] /= (pow2(particles[i*2] - pitchRoll[0]) 
					+ pow2(particles[i*2 + 1] - pitchRoll[1]));
		weights[i] += FLT_MIN;

		*sum_weights += weights[i];
	}
}
					
void predictUpdateGauss(float *particles, float *weights, float *meas, float *pitchRoll,
						float *sum_weights, int numParticles, float predict_std, float t, 
						struct gaussGenState *state){
	/*
		particles => Needs to be a pointer to an array of length 2*numParticles
		weights => Needs to be a pointer to an array of length numParticles
		meas => A pointer to acquired measurements => an array of length 9 in [accx, accy, accz, gyrox, gyroy, gryoz, magx, magy, magz] format
		pitchRoll => A pointer to an array that contains pitch and roll calculated from acc data
	*/
	*sum_weights = 0.f;
	for (int i = 0; i < numParticles; i++){
		particles[i*2] += meas[3] * t + gauss(state) * predict_std;
		particles[i*2 + 1] += meas[4] * t + gauss(state) * predict_std;

		
		if (particles[i*2] > 180)
			particles[i*2] -= 360;
		else if (particles[i*2] < -180)
			particles[i*2] += 360;

		if (particles[i*2 + 1] > 180)
			particles[i*2 + 1] -= 360;
		else if (particles[i*2 + 1] < -180)
			particles[i*2 + 1] += 360;
		
		weights[i] /= (pow2(particles[i*2] - pitchRoll[0]) 
					+ pow2(particles[i*2 + 1] - pitchRoll[1]));
		weights[i] += FLT_MIN;

		*sum_weights += weights[i];
	}
}
					
void resampleEstimate(	float **particles, float **copyParticles, float *weights, 
						float sum_weights, int numParticles, float resetWeights, 
						float *filterState){
	/*
		particles => address of pointer to _particles array
		copyParticles => address of pointer to _copyParticles array
		weights => Needs to be a pointer to an array of length numParticles
		filterState => Pointer to an array where the data will be written, array needs to be of length 2
	*/
	int k, num_copies;
	float w, residual, sum_residual, x;
	float residuals[numParticles];
	int indexes[numParticles];
	float *temp;
	
	k=0;
		
	w = numParticles * weights[0] / sum_weights;
	num_copies = (int)w;
	residual = w - num_copies;
	residuals[0] = residual;
	sum_residual = residual;

	for (int j=0; j<num_copies; j++){
		indexes[k++] = 0;
	}
	for (int i = 0; i < numParticles; i++){
		w = numParticles * weights[i] / sum_weights;
		num_copies = (int)w;
		residual = w - num_copies;
		residuals[i] = residual + residuals[i-1];
		sum_residual += residual;

		for (int j=0; j<num_copies; j++){
			indexes[k++] = i;
		}
	}

	while (k < numParticles){
		x = (float)rand() * sum_residual / RAND_MAX;
		indexes[k++] = searchSorted(residuals, x, numParticles);
	}

	filterState[0] = 0.f;
	filterState[1] = 0.f;
	for (int i=0; i<numParticles; i++){
		(*copyParticles)[i*2] = (*particles)[indexes[i]*2];
		(*copyParticles)[i*2 + 1] = (*particles)[indexes[i]*2 + 1];
		// resample happens after every step, which means that the weights will always be 
		// 1/numParticles at this step (because we just resampled right before estimating)
		filterState[0] += (*copyParticles)[i*2];
		filterState[1] += (*copyParticles)[i*2 + 1];
		weights[i] = resetWeights;
	}
	// swap array pointers
	temp = *particles;
	*particles = *copyParticles;
	*copyParticles = temp;

	filterState[0] /= numParticles;
	filterState[1] /= numParticles;

}
