//
//  CustomDeviceDll.h
//
//  This header holds declarations for the Custom Quadrature DDS control DLL
//
//

#ifndef CustomDeviceDll_H
#define CustomDeviceDll_H

#ifdef CUSTOM_DEVICE_DLL
	//  Rebuild of DLL
	#define  MODE __declspec(dllexport)
#else
	//  User access to DLL functions
	#define  MODE __declspec(dllimport)
#endif


#ifdef __cplusplus
extern "C"
{
#undef  EXPORT
#define EXPORT MODE _stdcall
#else
#define EXPORT MODE EXTERN_C _stdcall
#endif
//
//  Prototypes
//
//     Basic control
//
// Configuration
int EXPORT boardCount();
int EXPORT deviceOpen(int target);
int EXPORT deviceClose(int target);
int EXPORT startStream(int target);
int EXPORT stopStream(int target);
int EXPORT Parse(int target, int ohhgod);
int EXPORT arrayTest(int target);
int EXPORT Vpp(int target);
bool EXPORT dlc(int target);
int EXPORT loadSettings(int target);
int EXPORT setParams(int target, const char *param, double value);




#ifdef __cplusplus
}
#endif

#endif
