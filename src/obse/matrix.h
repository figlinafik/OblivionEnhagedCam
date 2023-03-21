#include "obse/NiTypes.h"

#define PI 3.14159265358979323846


// Multiplies two 3x3 matrices m1, m2 and stores the result in s
// Note: s should not be set to the same matrix as m1 or m2
void MatrixMultiply(NiMatrix33 * s, NiMatrix33 * m1, NiMatrix33 * m2);


// Multiplies a 3x3 matrix m by vector v and stores the result in s
// Note: s should not be set to the same vector as v
void MatrixVectorMultiply(NiVector3 * s, NiMatrix33 * m, NiVector3 * v);


// Calculates the inverse of matrix m, returns the result in s
// reference: http://en.wikipedia.org/wiki/Invertible_matrix
void MatrixInverse(NiMatrix33 * s, NiMatrix33 * m);


// Factors m as RxRyRz into Euler angles
void MatrixToEuler(NiVector3 * v, NiMatrix33 * m);


// Creates the rotation matrix in RxRyRz format
void EulerToMatrix(NiMatrix33 * m, NiVector3 * v);
