/*=====================================================================
Parser.cpp
----------
File created by ClassTemplate on Sat Jul 08 20:49:28 2006
Code By Nicholas Chapman.
=====================================================================*/
#include "Parser.h"


#include "../maths/mathstypes.h"


Parser::Parser(const char* text_, unsigned int textsize_)
:	text(text_),
	textsize(textsize_)
{
	currentpos = 0;
}


Parser::~Parser()
{
}


static const int ASCII_ZERO_INT = (int)'0';


bool Parser::parseInt(int& result_out)
{
	/// Parse sign ///
	int sign = 1;
	if(parseChar('+'))
	{}
	else if(parseChar('-'))
		sign = -1;

	unsigned int x = 0;
	const int initial_currentpos = currentpos;
	for( ;notEOF() && ::isNumeric(text[currentpos]); ++currentpos)
		x = 10*x + ((int)text[currentpos] - ASCII_ZERO_INT);
	result_out = sign * x;
	return currentpos - initial_currentpos > 0;
}




bool Parser::parseFloat(float& result_out)
{
	double x;
	const bool result = parseDouble(x);
	result_out = (float)x;
	return result;



	/*for(unsigned int i=0 ;notEOF() && !::isWhitespace(text[currentpos]) && i < PARSER_TMP_BUF_SIZE; ++currentpos, ++i)
		tmp[i] = text[currentpos];
	tmp[i] = '\0';

	result_out = (float)atof(tmp);
	return true;*/










	/*const char* start = text + currentpos;

	for(;notEOF(); ++currentpos)
	{
		if(isWhitespace(text[currentpos]))
		{
			char old = text[currentpos];
			text[currentpos] = '\0';//evil hax >:)	
			result_out = (float)atof(start);
			text[currentpos] = old;
			return true;
		}
	}
	return false;*/











	/*const char* startptr = text + currentpos;
	char* endptr;
	result_out = (float)strtod(startptr, &endptr);

	currentpos = endptr - text;

	return startptr != endptr;*/

	//char tmp[256];
/*
	for(unsigned int i=0 ;notEOF() && !::isWhitespace(text[currentpos]); ++currentpos, ++i)
		tmp[i] = text[currentpos];
	tmp[i] = '\0';
	

	result_out = (float)atof(tmp);
	//find next space
	//for( ;notEOF() && !::isWhitespace(text[currentpos]); ++currentpos)
	//{}
	//currentpos += i;

	return true;
*/


	
	
	/*const bool result = parseNonWSToken(temp);
	if(!result)
		return false;

	result_out = (float)atof(temp.c_str());
	return true;*/
	
	
	
	/*double d = 0.0;
	if(parseChar('+'))
	{}
	else if(parseChar('-'))
	{
		d *= -1.0;
	}

	for( ;currentpos < textsize && ::isNumeric(text[currentpos]); ++currentpos)
	{
		x = 10*x + ((int)text[currentpos] - (int)'0');
		found_int = true;
	}*/



	/*if(eof())
		return false;

	char* p = (char*)&text[currentpos];
	const int numread = sscanf(p, "%f", &result_out);

	/// parse sign
	if(parseChar('+') || parseChar('-'))
	{}

	int x;
	parseInt(x);
	if(parseChar('.'))
	{
		parseInt(x);
		if(parseChar('d') || parseChar('D') || parseChar('e') || parseChar('E'))
		{
			/// parse sign
			if(parseChar('+') || parseChar('-'))
			{}

			parseInt(x);
		}
	}

	
	return numread == 1;*/



	/*std::string s;
	if(parseChar('+'))
	{}
	else if(parseChar('-'))
	{
		concatWithChar(s, '-');
	}*/
	

}


bool Parser::fractionalNumberNext()
{
	const unsigned int initial_currentpos = currentpos;

	if(eof())
	{
		currentpos = initial_currentpos; // restore currentpos
		return false;
	}

	if(parseChar('+'))
	{}
	else if(parseChar('-'))
	{}

	if(eof())
	{
		currentpos = initial_currentpos; // restore currentpos
		return false;
	}

	while(notEOF() && isNumeric(current()))
	{
		currentpos++;
	}

	if(parseChar('.'))
	{
		currentpos = initial_currentpos; // restore currentpos
		return true;
	}
	else
	{
		currentpos = initial_currentpos; // restore currentpos
		return false;
	}
}


//[whitespace] [sign] [digits] [.digits] [ {d | D | e | E }[sign]digits]

//must be whitespace delimited
bool Parser::parseDouble(double& result_out)
{
	if(eof())
		return false;
	
	/// Parse sign ///
	double sign = 1.;
	
	if(parseChar('+'))
	{}
	else if(parseChar('-'))
		sign = -1.;

	if(eof())// || !isNumeric(current()))
		return false;

	bool reached_numerals = false;

	/// Parse optional digits before decimal point ///
	double x = 0.0;
	while(notEOF() && isNumeric(current()))
	{
		//shift left (in base 10) and add new digit.
		x = x * 10.0 + (double)((int)current() - ASCII_ZERO_INT);
		currentpos++;
		reached_numerals = true;
	}

	/// Parse optional decimal point + digits ///
	if(parseChar('.'))
	{
		//parse digits to right of decimal point
		double place_val = 0.1;
		while(notEOF() && isNumeric(current()))
		{
			x += place_val * (double)((int)current() - ASCII_ZERO_INT);
			currentpos++;
			place_val *= 0.1;
			reached_numerals = true;
		}
	}

	/// Parse optional exponent ///
	if(notEOF() && (current() == 'e' || current() == 'E' || current() == 'd' || current() == 'D'))
	{
		currentpos++;

		//parse optional sign
		double exponent_sign = 1.;
		if(parseChar('+'))
		{}
		else if(parseChar('-'))
			exponent_sign = -1.;

		if(eof())
			return false;

		//parse exponent digits
		int e = 0;
		while(notEOF() && isNumeric(current()))
		{
			e = e * 10 + ((int)current() - ASCII_ZERO_INT);
			currentpos++;
		}

		result_out = sign * x * pow(10.0, exponent_sign * (double)e);
	}
	else
	{
		//no exponent
		result_out = (sign * x);
	}

	return reached_numerals;

}

bool Parser::parseAlphaToken(std::string& token_out)
{
	token_out = "";
	bool found = false;
	for( ;notEOF() && ::isAlphabetic(text[currentpos]); ++currentpos)
	{
		::concatWithChar(token_out, text[currentpos]);
		found = true;
	}
	return found;
}

bool Parser::parseNonWSToken(std::string& token_out)
{
	token_out = "";
	bool found = false;
	for( ;notEOF() && !::isWhitespace(text[currentpos]); ++currentpos)
	{
		::concatWithChar(token_out, text[currentpos]);
		found = true;
	}
	return found;
}

bool Parser::parseString(const std::string& s)
{
	const unsigned int initial_pos = currentPos();

	for(unsigned int i=0; i<s.length(); ++i)
	{
		if(eof() || current() != s[i])
		{
			currentpos = initial_pos;
			return false;
		}
		advance();
	}
	return true;
}


/*bool Parser::parseString(const std::string& s)
{
	if(currentpos + s.size() >= textsize)
		return false;

	for(unsigned int i=0; i<s.size(); ++i)
}*/

void Parser::doUnitTests()
{
#ifdef _DEBUG
	{
	std::string text = "-456.5456e21 673.234 -0.5e-53 .6 167/2/3 4/5/6";

	Parser p(text.c_str(), text.size());

	float f;
	assert(p.parseFloat(f));
	assert(::epsEqual((float)f, -456.5456e21f));
	assert(p.parseWhiteSpace());

	assert(p.parseFloat(f));
	assert(::epsEqual((float)f, 673.234f));
	assert(p.parseWhiteSpace());

	assert(p.parseFloat(f));
	assert(::epsEqual((float)f, -0.5e-53f));
	assert(p.parseWhiteSpace());

	assert(p.parseFloat(f));
	assert(::epsEqual((float)f, .6f));
	assert(p.parseWhiteSpace());

	unsigned int x;
	assert(p.parseUnsignedInt(x));
	assert(x == 167);
	assert(p.parseChar('/'));
	assert(p.parseUnsignedInt(x));
	assert(x == 2);
	assert(p.parseChar('/'));

	assert(p.parseUnsignedInt(x));
	assert(x == 3);

	assert(p.parseWhiteSpace());

	assert(p.parseUnsignedInt(x));
	assert(x == 4);
	assert(p.parseChar('/'));
	assert(p.parseUnsignedInt(x));
	assert(x == 5);
	assert(p.parseChar('/'));
	assert(p.parseUnsignedInt(x));
	assert(x == 6);
	assert(p.eof());

	assert(!p.parseChar('/'));
	assert(!p.parseUnsignedInt(x));
	assert(!p.parseWhiteSpace());
	}


	{
	const std::string text = "123--456222";

	Parser p(text.c_str(), text.size());

	unsigned int x;
	assert(p.parseNDigitUnsignedInt(3, x));
	assert(x == 123);
	
	assert(p.parseChar('-'));
	assert(p.parseChar('-'));

	assert(p.parseNDigitUnsignedInt(3, x));
	assert(x == 456);

	assert(p.parseNDigitUnsignedInt(3, x));
	assert(x == 222);
	}

	{
	const std::string text = "bleh 123";
	Parser p(text.c_str(), text.size());

	double x;
	assert(!p.parseDouble(x));
	}

	{
	const std::string text = "123 -645 meh";
	Parser p(text.c_str(), text.size());

	int x;
	assert(p.parseInt(x));
	assert(x == 123);

	assert(p.parseWhiteSpace());

	assert(p.parseInt(x));
	assert(x == -645);
	
	assert(p.parseWhiteSpace());

	assert(!p.parseInt(x));
	}

	{
	const std::string text = "BLEH";
	Parser p(text.c_str(), text.size());

	assert(!p.parseString("BLEHA"));
	assert(p.parseString("BL"));
	assert(p.parseString("EH"));
	}

#endif
}









