/*=====================================================================
Timer.cpp
-------------------
Copyright Glare Technologies Limited 2014 -
=====================================================================*/
#include "Timer.h"


#include "StringUtils.h"


const std::string Timer::elapsedString() const // e.g "30.4 s"
{
	return toString(this->elapsed()) + " s";
}


const std::string Timer::elapsedStringNPlaces(int n) const // Print with n decimal places.
{
	return ::doubleToStringNDecimalPlaces(this->elapsed(), n) + " s";
}


const std::string Timer::elapsedStringNSigFigs(int n) const // Print with n decimal places.
{
	return ::doubleToStringNSigFigs(this->elapsed(), n) + " s";
}
