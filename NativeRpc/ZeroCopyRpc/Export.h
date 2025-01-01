#pragma once

#if defined(_WIN32) || defined(_WIN64)
	#ifdef BUILD_DLL
		#define EXPORT __declspec(dllexport)
	#else
		#define EXPORT __declspec(dllimport)
	#endif
#else
	#define EXPORT __attribute__((visibility("default")))
#endif
