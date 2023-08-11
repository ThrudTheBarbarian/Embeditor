//
//  macros.h
//  Embeditor
//
//  Created by Simon Gornall on 7/17/20.
//  Copyright © 2020 Simon Gornall. All rights reserved.
//

#ifndef macros_h
#define macros_h

#include <stdlib.h>

/*****************************************************************************\
|* These will release the memory they point to (from various allocators) and
|* then null out the pointer
\*****************************************************************************/
#define FREE(x)						do 								\
										{ 							\
										::free((void *)x); 			\
										x = nullptr; 				\
										} 							\
									while (false)

#define DELETE(x)					do 								\
										{ 							\
										delete x;			 		\
										x = nullptr; 				\
										} 							\
									while (false)

#define DELETE_ARRAY(x)				do 								\
										{ 							\
										delete [] x; 				\
										x = nullptr; 				\
										} 							\
									while (false)

/*****************************************************************************\
|* Xcode provides these sometimes, depending on whether you include some magic
|* header file...
\*****************************************************************************/
#ifndef MIN
#	define MIN(x,y)  (((x) < (y)) ? (x) : (y))
#endif

#ifndef ABS
#	define ABS(x)    (((x) < 0) ? -(x) : (x))
#endif

/*****************************************************************************\
|* It's just *so* hard to leave ObjC behind...
\*****************************************************************************/
#ifndef YES
#   define YES true
#endif

#ifndef NO
#   define NO false
#endif


/*****************************************************************************\
|* Why is this not in the language
\*****************************************************************************/
#define forever for(;;)


/*****************************************************************************\
|* Semaphore macros to make things more readable
\*****************************************************************************/
#define LOCK(x)    		 	xSemaphoreTakeRecursive(x, portMAX_DELAY) == pdTRUE
#define UNLOCK_FAILED(x)	xSemaphoreGiveRecursive(x) != pdTRUE

#endif /* macros_h */
