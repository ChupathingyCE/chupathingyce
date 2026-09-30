/*
XBOX_D3DX.C

The four D3DX functions the game calls. The SDK's D3DX library is built on
its own Direct3D, not the Aug 2001 one the game links with (tools/
xbox_build.py), so these are written out from D3DX's documented formulas
instead.

This file is compiled with the later SDK's headers, not the game's.
*/

#include <xtl.h>
#include <stdio.h>

D3DXMATRIX *WINAPI D3DXMatrixOrthoLH(
	D3DXMATRIX *out,
	FLOAT width,
	FLOAT height,
	FLOAT near_z,
	FLOAT far_z)
{
	ZeroMemory(out, sizeof(*out));
	out->_11= 2.0f/width;
	out->_22= 2.0f/height;
	out->_33= 1.0f/(far_z-near_z);
	out->_43= near_z/(near_z-far_z);
	out->_44= 1.0f;
	return out;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveLH(
	D3DXMATRIX *out,
	FLOAT width,
	FLOAT height,
	FLOAT near_z,
	FLOAT far_z)
{
	ZeroMemory(out, sizeof(*out));
	out->_11= 2.0f*near_z/width;
	out->_22= 2.0f*near_z/height;
	out->_33= far_z/(far_z-near_z);
	out->_34= 1.0f;
	out->_43= near_z*far_z/(near_z-far_z);
	return out;
}

/* the row vector times the matrix */
D3DXVECTOR4 *WINAPI D3DXVec4Transform(
	D3DXVECTOR4 *out,
	CONST D3DXVECTOR4 *vector,
	CONST D3DXMATRIX *matrix)
{
	D3DXVECTOR4 result;

	result.x= vector->x*matrix->_11 + vector->y*matrix->_21 + vector->z*matrix->_31 + vector->w*matrix->_41;
	result.y= vector->x*matrix->_12 + vector->y*matrix->_22 + vector->z*matrix->_32 + vector->w*matrix->_42;
	result.z= vector->x*matrix->_13 + vector->y*matrix->_23 + vector->z*matrix->_33 + vector->w*matrix->_43;
	result.w= vector->x*matrix->_14 + vector->y*matrix->_24 + vector->z*matrix->_34 + vector->w*matrix->_44;
	*out= result;
	return out;
}

HRESULT WINAPI D3DXGetErrorStringA(
	HRESULT error,
	LPSTR buffer,
	UINT buffer_length)
{
	if (!buffer || !buffer_length)
	{
		return D3DERR_INVALIDCALL;
	}

	_snprintf(buffer, buffer_length, "HRESULT 0x%08lX", (unsigned long)error);
	buffer[buffer_length-1]= 0;
	return S_OK;
}
