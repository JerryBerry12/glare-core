/*=====================================================================
singleton.h
-----------
File created by ClassTemplate on Mon Jun 03 15:02:43 2002
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __SINGLETON_H_666_
#define __SINGLETON_H_666_


#include <assert.h>
#include <stdlib.h>//NULL seems to want this

//if CYBERSPACE is defined, include globals.h for fatalError()
#ifdef CYBERSPACE
#include "../cyberspace/globals.h"
#endif

/*=====================================================================
Singleton
---------
Mixin based Singleton.
The template parameter should be set to the deriving class.
Do not use with program startup time objects.  (as instance may not have been set 
to null yet)

last changed 23 Jun 2004.

Send bugs/comments to nickamy@paradise.net.nz
http://homepages.paradise.net.nz/nickamy/
=====================================================================*/
template <class T>
class Singleton
{
public:
	/*=====================================================================
	Singleton
	---------
	
	=====================================================================*/
	Singleton(){}

	virtual ~Singleton(){}

	inline static void createInstance();
	inline static void createInstance(T* new_inst);
	inline static void destroyInstance();

	inline static T& getInstance();

	inline static bool isNull();

private:
	Singleton(const Singleton& other);
	Singleton& operator = (const Singleton& other);

	static T* instance;
};

template <class T>
T* Singleton<T>::instance = NULL;


template <class T>
void Singleton<T>::createInstance()
{
	assert(!instance);
	instance = new T(); 
}

template <class T>
void Singleton<T>::createInstance(T* new_inst)
{
	assert(!instance);
	instance = new_inst; 

}

template <class T>
void Singleton<T>::destroyInstance()
{
	delete instance;
	instance = NULL;
}

template <class T>
T& Singleton<T>::getInstance()
{
	//if(!instance)
	//	throw std::string("tried to access null singleton.");

	assert(instance);

	/*if(!instance)
	{
		#ifdef CYBERSPACE
		::fatalError("tried to dereference null singleton.");
		#endif

		//At this point, you're pretty fucked.  So just throw an exception
		//for want of anthing better to do.
		throw std::exception();//"tried to dereference null singleton.");
	}*/

	return *instance;
}

template <class T>
bool Singleton<T>::isNull()
{
	return instance == NULL;
}


#endif //__SINGLETON_H_666_




