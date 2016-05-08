#include "math/lin/quat.h"
#include "math/lin/matrix4x4.h"

void Quaternion::toMatrix(Matrix4x4 *out) const {
	Matrix4x4 temp;
	temp.setIdentity();
	float ww, xx, yy, zz, wx, wy, wz, xy, xz, yz;
	ww = w*w;	 xx = x*x;	 yy = y*y;	 zz = z*z; 
	wx = w*x*2; wy = w*y*2; wz = w*z*2;
	xy = x*y*2; xz = x*z*2; yz = y*z*2;

	temp.xx = ww + xx - yy - zz;
	temp.xy = xy + wz;
	temp.xz = xz - wy;

	temp.yx = xy - wz;
	temp.yy = ww - xx + yy - zz;
	temp.yz = yz + wx;

	temp.zx = xz + wy;
	temp.zy = yz - wx;
	temp.zz = ww - xx - yy + zz;

	*out = temp;
}

Quaternion Quaternion::fromMatrix(Matrix4x4 &m)
{
	// Algorithm in Ken Shoemake's article in 1987 SIGGRAPH course notes
	// article "Quaternion Calculus and Fast Animation".
	Quaternion q(0,0,0,1);
	/*
	float fTrace = m[0][0] + m[1][1] + m[2][2];
	float fRoot;

	if( fTrace > 0.0 )
	{
	fRoot = sqrtf( fTrace + 1.0f );

	q.w = 0.5f * fRoot;

	fRoot = 0.5f / fRoot;

	q.x = ( m[2][1] - m[1][2] ) * fRoot;
	q.y = ( m[0][2] - m[2][0] ) * fRoot;
	q.z = ( m[1][0] - m[0][1] ) * fRoot;
	}
	else
	{
	int iNext[3] = { 1, 2, 0 };

	int i = 0;
	if( m[1][1] > m[0][0] )
	i = 1;

	if( m[2][2] > m[i][i] )
	i = 2;

	int j = iNext[i];
	int k = iNext[j];

	fRoot = sqrtf( m[i][i] - m[j][j] - m[k][k] + 1.0f );

	float *apfQuat = &q.x;

	apfQuat[i] = 0.5f * fRoot;

	fRoot = 0.5f / fRoot;

	q.w = ( m[k][j] - m[j][k] ) * fRoot;

	apfQuat[j] = ( m[j][i] + m[i][j] ) * fRoot;
	apfQuat[k] = ( m[k][i] + m[i][k] ) * fRoot;
	}
	q.normalize(); */
	return q;
};

// TODO: Allegedly, lerp + normalize can achieve almost as good results.
Quaternion Quaternion::slerp(const Quaternion &to, const float a) const {
	Quaternion to2;
	float angle, cos_angle, scale_from, scale_to, sin_angle;

	cos_angle = (x * to.x) + (y * to.y) + (z * to.z) + (w * to.w);	//4D dot product

	if (cos_angle < 0.0f)
	{
		cos_angle = -cos_angle;
		to2.w = -to.w; to2.x = -to.x; to2.y = -to.y; to2.z = -to.z;
	}
	else
	{
		to2 = to;
	}

	if ((1.0f - fabsf(cos_angle)) > 0.00001f)
	{
		/* spherical linear interpolation (SLERP) */
		angle = acosf(cos_angle);
		sin_angle	= sinf(angle);
		scale_from = sinf((1.0f - a) * angle) / sin_angle;
		scale_to	 = sinf(a				 * angle) / sin_angle;
	}
	else
	{
		/* to prevent divide-by-zero, resort to linear interpolation */
		// This is okay in 99% of cases anyway, maybe should be the default?
		scale_from = 1.0f - a;
		scale_to	 = a;
	}

	return Quaternion(
		scale_from*x + scale_to*to2.x,
		scale_from*y + scale_to*to2.y,
		scale_from*z + scale_to*to2.z,
		scale_from*w + scale_to*to2.w
		);
}

Quaternion Quaternion::multiply(const Quaternion &q) const {
	return Quaternion((w * q.x) + (x * q.w) + (y * q.z) - (z * q.y),
		(w * q.y) + (y * q.w) + (z * q.x) - (x * q.z),
		(w * q.z) + (z * q.w) + (x * q.y) - (y * q.x),
		(w * q.w) - (x * q.x) - (y * q.y) - (z * q.z));
}
