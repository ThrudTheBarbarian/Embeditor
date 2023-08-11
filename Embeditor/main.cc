//
//  main.cc
//  Embeditor
//
//  Created by Simon Gornall on 8/10/23.
//
#include <iostream>
#include "Editor.h"

int main(int argc, const char * argv[])
	{
	Editor e;
	if (argc > 1)
		e.open(argv[1]);
	e.edit();
	
	return 0;
	}
