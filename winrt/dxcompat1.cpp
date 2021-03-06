#include <windows.h>
//#include <windowsx.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <math.h>
#include <vector>
//#include "globals.h"
#include "dxcompat.hpp"
//#include "util.h"

#include "glog.h"

// This is needed for WinRT apps
#ifdef WINSTORE
#include <memory>
#include "pch.h"
using namespace Microsoft::WRL;
#endif

using namespace std;

class Mat4
{
public:
  float M[4][4];

  Mat4()
  {
    int i,j;
    for (i=0;i<4;i++)
      for (j=0;j<4;j++)
    	M[i][j]=0;
  }

  void loadIdentity()
  {
    int i,j;
    for (i=0;i<4;i++){
      for (j=0;j<4;j++){
	if (i==j)
	  M[i][j]=1;
	else
	  M[i][j]=0;
      }
    }
  }

  void set(const GLfloat *m)
  {
	  int i, j,ind;

	  ind = 0;
	  for (j = 0; j < 4; j++){
		  for (i = 0; i < 4; i++){
			  M[i][j] = m[ind];
			  ind++;
		  }
	  }
  }

  void mulrightby(Mat4 A)
  {
    float B[4][4];
    int i,j,k;

    // B=M*A
    for (i=0;i<4;i++){
      for (j=0;j<4;j++){
	B[i][j]=0;
	for (k=0;k<4;k++){
	  B[i][j]+=M[i][k]*A.M[k][j];
	}
      }
    }

    // M=B
    for (i=0;i<4;i++)
      for (j=0;j<4;j++)
	M[i][j]=B[i][j];
  }

  void mulleftby(Mat4 A)
  {
    float B[4][4];
    int i,j,k;

    // B=A*M
    for (i=0;i<4;i++){
      for (j=0;j<4;j++){
	B[i][j]=0;
	for (k=0;k<4;k++){
	  B[i][j]+=A.M[i][k]*M[k][j];
	}
      }
    }

    // M=B
    for (i=0;i<4;i++)
      for (j=0;j<4;j++)
	M[i][j]=B[i][j];
  }
};  

// ######################################################################

// The following parameters must be set by main program before we draw anything
// For Windows Store and WP8, we use 11.1 objects for g_dev, g_devcon, g_swapchain

#ifdef WINSTORE
ComPtr<ID3D11Device1> g_dev;                     // the pointer to our Direct3D device interface (11.1)
ComPtr<ID3D11DeviceContext1> g_devcon;           // the pointer to our Direct3D device context (11.1)
ComPtr<IDXGISwapChain1> g_swapchain;             // the pointer to the swap chain interface (11.1)
ComPtr<IDXGIDevice3> dxgiDevice;
#else
ID3D11Device *g_dev;                     // the pointer to our Direct3D device interface
ID3D11DeviceContext *g_devcon;           // the pointer to our Direct3D device context
IDXGISwapChain *g_swapchain;             // the pointer to the swap chain interface
#endif

ID3D11RenderTargetView *g_backbuffer;
ID3D11InputLayout *g_pLayout;
ID3D11VertexShader *g_pVS;
ID3D11PixelShader *g_pPS;
ID3D11Buffer *g_pVBuffer;                  // Vertex buffer: we put our geometry here
ID3D11Buffer *g_CB;                        // Constant buffer: pass settings like whether to use textures or not
vector<ID3D11ShaderResourceView*> g_RSV;   // list of textures.
vector<bool> g_RSVused;                    // true if texture index number has been assigned

struct Backcol
{
	float red;
	float green;
	float blue;
	float alpha;
};

vector<ID3D11RenderTargetView*> g_renderTarget;
vector<bool> g_renderTargetused;
vector<Backcol> g_renderTargetCol;

ID3D11SamplerState *g_samplerLinear;
ID3D11BlendState *g_pBlendState;

bool dxcompat_force_lines = false;
bool dxcompat_zrange01 = true;

int dxcompat_maxvertices = 16384;

// "OpenGL" state machine
static float g_r=1, g_g=1, g_b=1, g_a=1;
static bool g_color_array=false, g_tex_array=false;
static bool g_modelview=true;
static Mat4 modelmat,projectionmat;
static GLuint g_curr_texind, g_curr_framebuffer=0;
vector<Mat4> modelStack,projectionStack;

float backcol[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

static float *g_colors;
static float *g_vertices;
static float *g_tex_coord;

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
  D3D11_VIEWPORT viewport;
  ZeroMemory(&viewport,sizeof(D3D11_VIEWPORT));
  
  viewport.TopLeftX=x;
  viewport.TopLeftY=y;
  viewport.Width=width;
  viewport.Height=height;
  g_devcon->RSSetViewports(1,&viewport);
}

void glClear(GLbitfield mask){

	if (mask != GL_COLOR_BUFFER_BIT) return;   // no depth/stencil buffer yet

	if (g_curr_framebuffer == 0){
		g_devcon->ClearRenderTargetView(g_backbuffer, backcol);
	}
	else{
		float col[4];
		col[0] = g_renderTargetCol[g_curr_framebuffer].red;
		col[1] = g_renderTargetCol[g_curr_framebuffer].green;
		col[2] = g_renderTargetCol[g_curr_framebuffer].blue;
		col[3] = g_renderTargetCol[g_curr_framebuffer].alpha;

		g_devcon->ClearRenderTargetView(g_renderTarget[g_curr_framebuffer], col);
	}
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	if (g_curr_framebuffer == 0){
		backcol[0] = red;
		backcol[1] = green;
		backcol[2] = blue;
		backcol[3] = alpha;
	}
	else {
		g_renderTargetCol[g_curr_framebuffer].red = red;
		g_renderTargetCol[g_curr_framebuffer].green = green;
		g_renderTargetCol[g_curr_framebuffer].blue = blue;
		g_renderTargetCol[g_curr_framebuffer].alpha = alpha;
	}
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val)
{
  Mat4 O;

  O.loadIdentity();
  O.M[0][0] = 2.0 / (right - left);
  O.M[1][1] = 2.0 / (top - bottom);

  O.M[0][3] = -(right + left) / (right - left);
  O.M[1][3] = -(top + bottom) / (top - bottom);

  if (dxcompat_zrange01){
    O.M[2][2] = 1.0 /(far_val - near_val);
    O.M[2][3] = -near_val / (far_val - near_val);
  }
  else {
    O.M[2][2] = 2.0 / (far_val - near_val);   // -2.0 used in OpenGL documentation but can't be right!
    O.M[2][3] = -(far_val + near_val) / (far_val - near_val);
  }

  if (g_modelview)
    modelmat.mulrightby(O);
  else
    projectionmat.mulrightby(O);
}

void glLoadIdentity()
{
  if (g_modelview){
    modelmat.loadIdentity();
  }
  else {
    projectionmat.loadIdentity();
  }
}

void glMatrixMode(GLenum mode)
{
  if (mode==GL_MODELVIEW)  // normal drawing matrix
    g_modelview=true;
  else
    g_modelview=false;     // matrix to use orthographic transform on
}

void glTranslatef(GLfloat x,GLfloat y, GLfloat z)
{
  Mat4 T;

  T.loadIdentity();

  T.M[0][3]=x;
  T.M[1][3]=y;
  T.M[2][3]=z;

  if (g_modelview)
    modelmat.mulrightby(T);
  else
    projectionmat.mulrightby(T);
}

void glRotatef(GLfloat angle,GLfloat x,GLfloat y, GLfloat z)
{
  Mat4 R;
  const float pi=3.141592654;

  float ca=cos(angle*pi/180);
  float sa=sin(angle*pi/180);

  R.loadIdentity();
  
  R.M[0][0]=ca;  R.M[0][1]=-sa;
  R.M[1][0]=sa;  R.M[1][1]= ca;

  if (g_modelview)
    modelmat.mulrightby(R);
  else
    projectionmat.mulrightby(R);
}

void glScalef(GLfloat x,GLfloat y, GLfloat z)
{
  Mat4 S;

  S.loadIdentity();
  S.M[0][0]=x;
  S.M[1][1]=y;

  if (g_modelview)
    modelmat.mulrightby(S);
  else
    projectionmat.mulrightby(S);
}

void glPushMatrix()
{
  if (g_modelview)
    modelStack.push_back(modelmat);
  else
    projectionStack.push_back(projectionmat);
}

void glPopMatrix()
{
  if (g_modelview){
    modelmat=modelStack.back();
    modelStack.pop_back();
  } else {
    projectionmat=modelStack.back();
    projectionStack.pop_back();
  }
}

// GenTextures adds new texture RSV to list g_RSV and sets to NULL
// also sets g_RSVused to true so that the same index will not be reissued
// does not actually allocate any memory. Done in glTexImage2D
// glGenTextures(2,myarray);
// g_RSV:
// 0 NULL true
// 1 NULL true (myarray[0],myarray[1]=0,1)
// glTexImage2D (1=current)
// 0 = NULL  true
// 1 = 0x111 true
// glDeleteTextures(1,&mytexind); mytexind=0
// 0 = NULL  false// deleted  << CAN BE REUSED by another glGenTextures
// 1 = 0x111 true // keeps same number

void glGenTextures(GLsizei n, GLuint *texinds)
{
  int i,j;  // i: index in texinds, j: index in g_RSV

  i=0;
  for (j=0;j<g_RSV.size();j++){
	  if (!g_RSVused[j]){
		  g_RSV[j] = NULL;
		  g_RSVused[j] = true;
		  texinds[i] = j;
		  i++;
		  if (i == n) return;
	  }
  }

  while (true){
    g_RSV.push_back(NULL);
	g_RSVused.push_back(true);
    texinds[i]=j;
    i++;
    j++;
    if (i==n) return;
  }
}

// eg glDeleteTextures(2,{3,4})
void glDeleteTextures(GLsizei n, GLuint *texinds)
{
  int i,count;

  for (i=0;i<n;i++){
    GLuint mytexind=texinds[i];
	if (mytexind < g_RSV.size()){
		if (g_RSV[mytexind] != NULL) g_RSV[mytexind]->Release();
		g_RSV[mytexind] = NULL;
		g_RSVused[mytexind] = false;
	}
  }

  // remove end of list if full of unused slots
  count=0;
  for (i=g_RSV.size()-1;i>=0;i--){
    if (g_RSVused[i]) break;
    count++;
  }

  for (i = 0; i < count; i++){
	  g_RSV.pop_back();
	  g_RSVused.pop_back();
  }
}

void glBindTexture(GLenum target, GLuint texind)
{

	if (target != GL_TEXTURE_2D){
		glog_e("glBindTexture: wrong target\n");
		exit(1);
	}

	if (texind >= g_RSV.size()) return;
	if (!g_RSVused[texind]) return;

    g_curr_texind=texind;

//	if (g_RSV[texind]!=NULL)
		g_devcon->PSSetShaderResources(0,1,&g_RSV[texind]);
}

// bind current texture g_curr_texind
void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{

  ID3D11Texture2D *tex;
  D3D11_TEXTURE2D_DESC tdesc;
  D3D11_SUBRESOURCE_DATA tbsd;

  char *pixels1,*pixels2;
  int i;

  if (target != GL_TEXTURE_2D){
	  glog_e("glTexImage2D: unknown target\n");
	  exit(1);
  }

  if (level != 0) glog_w("glTexImage2D: level not zero\n");

  if (border != 0) {
	  glog_e("glTexImage2D: border must be zero\n");
	  exit(1);
  }

  if (format != internalFormat) glog_w("glTexImage2D: Warning format, internalFormat different\n");

  if (type != GL_UNSIGNED_BYTE) glog_w("glTexImage2D: unexpected pixel data type\n");

  pixels1 = (char *)pixels;

//  for (int i = 0; i < 400;i++)
//	  pixels1[i] = 255;

  tbsd.SysMemPitch = width * 4;
  tbsd.SysMemSlicePitch = width*height * 4; // not needed

  tdesc.Width = width;
  tdesc.Height = height;
  tdesc.MipLevels = 1;
  tdesc.ArraySize = 1;
  tdesc.SampleDesc.Count = 1;
  tdesc.SampleDesc.Quality = 0;
  tdesc.Usage = D3D11_USAGE_DEFAULT;
  tdesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  tdesc.CPUAccessFlags = 0;
  tdesc.MiscFlags = 0;
  tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

  if (internalFormat==GL_RGBA){
	tbsd.pSysMem = pixels;
  }
  else if (internalFormat==GL_RGB){
	  pixels2 = (char *)malloc(width*height * 4);
	  for (i = 0; i < width*height; i++){
		  pixels2[i * 4] = pixels1[i * 3];
		  pixels2[i * 4+1] = pixels1[i * 3+1];
		  pixels2[i * 4+2] = pixels1[i * 3+2];
		  pixels2[i * 4+3] = 255;
	  }
	  tbsd.pSysMem = pixels2;
  }
  else {
	  glog_w("glTexImage2D: unknown internal format");
	  exit(1);
  }

  g_dev->CreateTexture2D(&tdesc,&tbsd,&tex);
  g_dev->CreateShaderResourceView(tex,NULL,&g_RSV[g_curr_texind]);
  tex->Release();  // We only need the resource view

  g_devcon->PSSetShaderResources(0,1,&g_RSV[g_curr_texind]);

  if (internalFormat == GL_RGB) free(pixels2);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param )
{
}

void glPixelStorei(GLenum pname, GLint param)
{
}

void glColor4ub(GLubyte r,GLubyte g, GLubyte b, GLubyte a)
{
  g_r=r/255.0;
  g_g=g/255.0;
  g_b=b/255.0;
  g_a=a/255.0;
}

//######################################################################
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
  g_r=r;
  g_g=g;
  g_b=b;
  g_a=a;
}

//######################################################################
void glEnableClientState(GLenum type)
{
  struct const_buffer mycb;

  if (type==GL_COLOR_ARRAY)
    g_color_array=true;
  else if (type==GL_TEXTURE_COORD_ARRAY){
    g_tex_array=true;
    mycb.use_tex=1;
    g_devcon->UpdateSubresource(g_CB,0,NULL,&mycb,0,0);
    g_devcon->PSSetConstantBuffers(0,1,&g_CB);
  }
}

//######################################################################
void glDisableClientState(GLenum type)
{

  struct const_buffer mycb;

  if (type==GL_COLOR_ARRAY)
    g_color_array=false;
  else if (type==GL_TEXTURE_COORD_ARRAY){
    g_tex_array=false;
    mycb.use_tex=0;
    g_devcon->UpdateSubresource(g_CB,0,NULL,&mycb,0,0);
    g_devcon->PSSetConstantBuffers(0,1,&g_CB);
  }
}

void glEnable(GLenum type)
{
}

void glDisable(GLenum type)
{
}

//######################################################################
void glDrawArrays(GLenum pattern, GLint zero, GLsizei npoints)
{

  // temporary: loop over npoint vertices and transform manually using
  // matrix multiplication

  int i,j,k;
  float x,y,xp,yp,zp;

//  static VERTEX DxVertices[1024];
  D3D11_MAPPED_SUBRESOURCE ms;    
  VERTEX *DxVertices;

  float mat[4][4];

  if (npoints > dxcompat_maxvertices) npoints = dxcompat_maxvertices;  // avoid overflow

  for (i=0;i<4;i++){
    for (j=0;j<4;j++){
      mat[i][j]=0;
      for (k=0;k<4;k++){
	mat[i][j]+=projectionmat.M[i][k]*modelmat.M[k][j];
      }
    }
  }

  g_devcon->Map(g_pVBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &ms);    // map the buffer    
  DxVertices = (VERTEX *)ms.pData;

  for (i=0;i<npoints;i++){
    x=g_vertices[2*i];
    y=g_vertices[2*i+1];

    xp=mat[0][0]*x+mat[0][1]*y+mat[0][3];   // assuming z=0, w=1
    yp=mat[1][0]*x+mat[1][1]*y+mat[1][3];
    zp=mat[2][0]*x+mat[2][1]*y+mat[2][3];

    DxVertices[i].X=xp;
    DxVertices[i].Y=yp;
    DxVertices[i].Z=zp;

    if (g_color_array){
      DxVertices[i].r=g_colors[4*i];
      DxVertices[i].g=g_colors[4*i+1];
      DxVertices[i].b=g_colors[4*i+2];
      DxVertices[i].a=g_colors[4*i+3];
    }
    else {
      DxVertices[i].r=g_r;
      DxVertices[i].g=g_g;
      DxVertices[i].b=g_b;
      DxVertices[i].a=g_a;
    }

    if (g_tex_array){
      DxVertices[i].u=g_tex_coord[2*i];
      DxVertices[i].v=g_tex_coord[2*i+1];
    }
  }

//  memcpy(ms.pData, DxVertices, sizeof(VERTEX)*npoints);                 // copy the data    
  g_devcon->Unmap(g_pVBuffer, NULL);                                      // unmap the buffer
  
  UINT stride=sizeof(VERTEX);
  UINT offset=0;

  g_devcon->IASetVertexBuffers(0,1,&g_pVBuffer,&stride, &offset);

  if (dxcompat_force_lines)
     pattern = GL_LINE_STRIP;

  if (pattern == GL_POINTS)
	  g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  else if (pattern == GL_TRIANGLE_STRIP)
	  g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  else if (pattern == GL_TRIANGLES)
	  g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  else if (pattern == GL_LINE_STRIP)
	  g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
  else if (pattern == GL_LINE_LOOP)
	  g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
  else if (pattern==GL_LINES)
	  g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  else {
	  glog_e("glDrawArrays unknown pattern\n");
	  exit(1);
  }

  g_devcon->Draw(npoints,0);
}

void glLineWidth(GLfloat width)
{
}

// ================ NEW =========================

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{

}

void glLoadMatrixf(const GLfloat *m)
{
	if (g_modelview)
		modelmat.set(m);
	else
		projectionmat.set(m);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{

	if (size != 4){
		glog_e("glColorPointer: size must be 4\n");
		exit(1);
	}

	if (type == GL_FLOAT)
		g_colors = (GLfloat*)ptr;
	else {
		glog_e("glColorPointer: illegal type\n");
		exit(1);
	}

	if (stride != 0) glog_w("glColorPointer. Stride should be zero\n");
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{

	if (size != 2){
		glog_e("glVertexPointer: size must be 2\n");
		exit(1);
	}

	if (type==GL_FLOAT)
    	g_vertices = (GLfloat*)ptr;
	else {
		glog_e("glVertexPointer: illegal type\n");
		exit(1);
	}

	if (stride != 0) glog_w("glVertexPointer. Stride should be zero\n");
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{

	if (size != 2){
		glog_e("glTexCoordPointer: size must be 2\n");
		exit(1);
	}

	if (type==GL_FLOAT)
	   g_tex_coord = (GLfloat*)ptr;
	else {
		glog_e("glTexCoordPointer: illegal type\n");
		exit(1);
	}

	if (stride != 0) glog_w("glVertexTexCoordPointer. Stride should be zero\n");

}

void glGetIntegerv(GLenum pname, GLint *params)
{
	if (pname == GL_TEXTURE_BINDING_2D)
		*params = g_curr_texind;
	else if (pname == GL_FRAMEBUFFER_BINDING)
		*params = g_curr_framebuffer;
	else {
		glog_w("Warning, glGetIntegerv pname not supported\n");
		*params = 0;
	}
}

const GLubyte *glGetString(GLenum name)
{
	glog_w("glGetString not supported\n");
	return NULL;
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
	glog_v("glTexEnvi not supported\n");
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid * indices)
{
	int i, j, k;
	float x, y, xp, yp, zp;

//	static VERTEX DxVertices[1024];
	VERTEX *DxVertices;
	D3D11_MAPPED_SUBRESOURCE ms;

	float mat[4][4];

	GLubyte *ub_indices;
	GLushort *us_indices;
	GLuint *ui_indices, ui_index;

	if (count > dxcompat_maxvertices) count = dxcompat_maxvertices;  // prevent overflow

	if (type == GL_UNSIGNED_BYTE)
		ub_indices = (GLubyte *)indices;
	else if (type==GL_UNSIGNED_SHORT)
		us_indices = (GLushort *)indices;
	else
		ui_indices = (GLuint *)indices;

	for (i = 0; i<4; i++){
		for (j = 0; j<4; j++){
			mat[i][j] = 0;
			for (k = 0; k<4; k++){
				mat[i][j] += projectionmat.M[i][k] * modelmat.M[k][j];
			}
		}
	}

	g_devcon->Map(g_pVBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &ms);    // map the buffer    
	DxVertices = (VERTEX *)ms.pData;

	for (i = 0; i<count; i++){

		if (type == GL_UNSIGNED_BYTE)
			ui_index = ub_indices[i];
		else if (type == GL_UNSIGNED_SHORT)
			ui_index = us_indices[i];
		else
			ui_index = ui_indices[i];

		x = g_vertices[2 * ui_index];
		y = g_vertices[2 * ui_index + 1];

		xp = mat[0][0] * x + mat[0][1] * y + mat[0][3];
		yp = mat[1][0] * x + mat[1][1] * y + mat[1][3];
		zp = mat[2][0] * x + mat[2][1] * y + mat[2][3];

		DxVertices[i].X = xp;
		DxVertices[i].Y = yp;
		DxVertices[i].Z = zp;

		if (g_color_array){
			DxVertices[i].r = g_colors[4 * ui_index];
			DxVertices[i].g = g_colors[4 * ui_index + 1];
			DxVertices[i].b = g_colors[4 * ui_index + 2];
			DxVertices[i].a = g_colors[4 * ui_index + 3];
		}
		else {
			DxVertices[i].r = g_r;
			DxVertices[i].g = g_g;
			DxVertices[i].b = g_b;
			DxVertices[i].a = g_a;
		}

		if (g_tex_array){
			DxVertices[i].u = g_tex_coord[2 * ui_index];
			DxVertices[i].v = g_tex_coord[2 * ui_index + 1];
		}
	}

//	memcpy(ms.pData, DxVertices, sizeof(VERTEX)*count);                 // copy the data    
	g_devcon->Unmap(g_pVBuffer, NULL);                                      // unmap the buffer

	UINT stride = sizeof(VERTEX);
	UINT offset = 0;

	g_devcon->IASetVertexBuffers(0, 1, &g_pVBuffer, &stride, &offset);

	if (dxcompat_force_lines)
		mode = GL_LINE_STRIP;

	if (mode == GL_POINTS)
		g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
	else if (mode == GL_TRIANGLE_STRIP)
		g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	else if (mode == GL_TRIANGLES)
		g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	else if (mode == GL_LINE_STRIP)
		g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
	else if (mode == GL_LINE_LOOP)
		g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
	else if (mode == GL_LINES)
		g_devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	else {
		glog_e("glDrawElements: bad mode\n");
		exit(1);
	}


	g_devcon->Draw(count, 0);

}


void glBindBuffer(GLenum target, GLuint buffer)
{
}

void glDeleteFramebuffers(GLsizei n, GLuint *framebuffers)
{
	int i, count;

	for (i = 0; i<n; i++){
		GLuint mytexind = framebuffers[i];
		if (mytexind < g_renderTarget.size()){
			if (g_renderTarget[mytexind] != NULL) g_renderTarget[mytexind]->Release();   // delete rendertarget
			g_renderTarget[mytexind] = NULL;
			g_renderTargetused[mytexind] = false;
		}
	}

	// remove end of list if full of unused slots
	count = 0;
	for (i = g_renderTarget.size() - 1; i >= 0; i--){
		if (g_renderTargetused[i]) break;
		count++;
	}

	for (i = 0; i < count; i++){
		g_renderTarget.pop_back();
		g_renderTargetused.pop_back();
		g_renderTargetCol.pop_back();
	}

}

void glGenFramebuffers(GLsizei n, GLuint *framebuffers)
{
	int i, j;  // i: index in framebuffers, j: index in g_renderTarget
	Backcol col = { 1.0f, 1.0f, 1.0f, 1.0f };

	i = 0;
	for (j = 0; j<g_renderTarget.size(); j++){
		if (!g_renderTargetused[j]){
			g_renderTarget[j] = NULL;
			g_renderTargetused[j] = true;
			g_renderTargetCol[j] = col;

			framebuffers[i] = j;
			i++;
			if (i == n) return;
		}
	}

	while (true){
		g_renderTarget.push_back(NULL);
		g_renderTargetused.push_back(true);
		g_renderTargetCol.push_back(col);

		framebuffers[i] = j;
		i++;
		j++;
		if (i == n) return;
	}

}

void glBindFramebuffer(GLenum target, GLuint framebuffer)
{
	if (target != GL_FRAMEBUFFER){
		glog_e("glBindFramebuffer: wrong target\n");
		exit(1);
	}

	if (framebuffer == 0){
		g_devcon->OMSetRenderTargets(1, &g_backbuffer, NULL);  // draw on screen (actually back buffer)
		g_curr_framebuffer = 0;
		return;
	}

	if (framebuffer >= g_renderTarget.size()) return;
	if (!g_renderTargetused[framebuffer]) return;

	g_curr_framebuffer = framebuffer;

	if (g_renderTarget[g_curr_framebuffer]!=NULL)
		g_devcon->OMSetRenderTargets(1, &g_renderTarget[g_curr_framebuffer], NULL);  // draw on texture
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{

	D3D11_SHADER_RESOURCE_VIEW_DESC desc;
	ID3D11Resource *res;

	if (target != GL_FRAMEBUFFER){
		glog_e("glFramebufferTexture2D: bad target\n");
		exit(1);
	}

	if (attachment != GL_COLOR_ATTACHMENT0){
		glog_e("glFramebufferTexture2D: bad attachment\n");
		exit(1);
	}

	if (textarget != GL_TEXTURE_2D){
		glog_e("glFramebufferTexture2D: bad textarget\n");
		exit(1);
	}

	if (level != 0){
		glog_e("glFramebufferTexture2D: bad level\n");
		exit(1);
	}

	if (g_RSV[texture] == NULL) return;

	g_RSV[texture]->GetDesc(&desc);
	g_RSV[texture]->GetResource(&res);

	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;

	ZeroMemory(&renderTargetViewDesc, sizeof(renderTargetViewDesc));

	renderTargetViewDesc.Format = desc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	g_dev->CreateRenderTargetView(res, &renderTargetViewDesc, &g_renderTarget[g_curr_framebuffer]);  // create rendertarget
	g_devcon->OMSetRenderTargets(1, &g_renderTarget[g_curr_framebuffer], NULL);   // draw on texture
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{

}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	glog_v("glTexParameteri not supported\n");
}


// Empty for now
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{

}

void glDepthFunc(GLenum func)
{

}


// ######################################################################
// On WinRT these functions do not exist (headers are present for compiling)
// but they are needed by Lua so put do-nothing functions for link stage

#ifdef WINSTORE
extern "C"{
  char *getenv(const char *string){ return NULL; }
  int system(const char *string){ return 0; }
}
#endif
