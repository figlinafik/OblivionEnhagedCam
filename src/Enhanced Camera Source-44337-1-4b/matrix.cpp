#include "matrix.h"


void MatrixMultiply(NiMatrix33 * s, NiMatrix33 * m1, NiMatrix33 * m2) {
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 3; x++) {
			s->data[3*y + x] = 0;
			for (int i = 0; i < 3; i++) {
				s->data[3*y + x] += m1->data[3*y + i] * m2->data[3*i + x];
			}
		}
	}
}


void MatrixVectorMultiply(NiVector3 * s, NiMatrix33 * m, NiVector3 * v) {
	s->x = m->data[0] * v->x + m->data[1] * v->y + m->data[2] * v->z;
	s->y = m->data[3] * v->x + m->data[4] * v->y + m->data[5] * v->z;
	s->z = m->data[6] * v->x + m->data[7] * v->y + m->data[8] * v->z;
}


void MatrixInverse(NiMatrix33 * s, NiMatrix33 * m) {
	const float a = m->data[0];
	const float b = m->data[1];
	const float c = m->data[2];
	const float d = m->data[3];
	const float e = m->data[4];
	const float f = m->data[5];
	const float g = m->data[6];
	const float h = m->data[7];
	const float k = m->data[8];

	float det = a*(e*k - f*h) - b*(d*k - f*g) + c*(d*h - e*g);

	s->data[0] = (e*k - f*h)/det;
	s->data[1] = (c*h - b*k)/det;
	s->data[2] = (b*f - c*e)/det;
	s->data[3] = (f*g - d*k)/det;
	s->data[4] = (a*k - c*g)/det;
	s->data[5] = (c*d - a*f)/det;
	s->data[6] = (d*h - e*g)/det;
	s->data[7] = (g*b - a*h)/det;
	s->data[8] = (a*e - b*d)/det;
}


void MatrixToEuler(NiVector3 * v, NiMatrix33 * m) {
	if (m->data[2] < +1) {
		if (m->data[2] > -1) {
			v->y = asin(m->data[2]);
			v->x = atan2(-m->data[5],m->data[8]);
			v->z = atan2(-m->data[1],m->data[0]);
		} else {
			v->y = -PI/2;
			v->x = -atan2(m->data[3],m->data[4]);
			v->z = 0;
		}
	} else {
		v->y = +PI/2;
		v->x = atan2(m->data[3],m->data[4]);
		v->z = 0;
	}
}


void EulerToMatrix(NiMatrix33 * m, NiVector3 * v) {
	m->data[0] = cos(v->y)*cos(v->z);
	m->data[1] = -cos(v->y)*sin(v->z);
	m->data[2] = sin(v->y);
	m->data[3] = cos(v->z)*sin(v->x)*sin(v->y) + cos(v->x)*sin(v->z);
	m->data[4] = cos(v->x)*cos(v->z) - sin(v->x)*sin(v->y)*sin(v->z);
	m->data[5] = -cos(v->y)*sin(v->x);
	m->data[6] = -cos(v->x)*cos(v->z)*sin(v->y) + sin(v->x)*sin(v->z);
	m->data[7] = cos(v->z)*sin(v->x) + cos(v->x)*sin(v->y)*sin(v->z);
	m->data[8] = cos(v->x)*cos(v->y);
}
