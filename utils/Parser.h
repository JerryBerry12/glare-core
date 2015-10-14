/*=====================================================================
Parser.h
--------
Copyright Glare Technologies Limited 2015 - 
=====================================================================*/
#pragma once


#include "../utils/StringUtils.h"
#include "../utils/Platform.h"
#include "../utils/string_view.h"
#include <string>
#include <assert.h>


/*=====================================================================
Parser
------

=====================================================================*/
class Parser
{
public:
	Parser();
	Parser(const char* text, unsigned int textsize);

	~Parser();

	void reset(const char* text, unsigned int textsize);

	// Returns if character was found.
	// If it was, currentpos will be pointing to the next character.
	// If not, it won't be moved.
	inline bool parseChar(char target);
	bool parseInt(int32& result_out);
	bool parseInt64(int64& result_out);
	bool parseUnsignedInt(uint32& result_out);
	bool parseFloat(float& result_out);
	bool parseDouble(double& result_out);
	inline void parseWhiteSpace();
	inline void parseSpacesAndTabs();
	inline void advancePastLine();

	// Advances until current() == target, or EOF is reached.
	// Returns true iff EOF was not reached.
	// returns in 'result_out' a string from current to target, not including target.
	inline bool parseToChar(char target, string_view& result_out);
	inline bool parseToOneOfChars(char target_a, char target_b, string_view& result_out);
	inline void parseToCharOrEOF(char target_a, string_view& result_out);
	inline void parseLine();
	
	bool parseAlphaToken(string_view& token_out); // Parse alphabetic token
	bool parseIdentifier(string_view& token_out);
	bool parseNonWSToken(string_view& token_out);
	bool parseString(const std::string& s);
	bool parseCString(const char* const s);

	bool fractionalNumberNext();

	// Only defined if eof() is not true.
	inline char current() const;
	inline unsigned int currentPos() const { return currentpos; }

	inline bool eof() const;
	inline bool notEOF() const;
	inline void advance(); // Advance current forwards one character

	inline bool currentIsChar(char c) const { return notEOF() && current() == c; }


	inline bool nextIsEOF() const { return currentpos + 1 >= textsize; }
	inline bool nextIsNotEOF() const { return currentpos + 1 < textsize; }
	inline char next() const { assert(currentpos + 1 < textsize); return text[currentpos + 1]; }
	inline bool nextIsChar(char c) const { return nextIsNotEOF() && next() == c; }

	inline char prev() const { assert(currentpos >= 1); return text[currentpos - 1]; }

	inline const char* getText() const { return text; }
	inline unsigned int getTextSize() const { return textsize; }

	static void doUnitTests();
private:
	const char* text;
	unsigned int currentpos;
	unsigned int textsize;
	//char decimal_separator;
};


bool Parser::parseChar(char target)
{
	if(currentpos < textsize && text[currentpos] == target)
	{
		++currentpos;
		return true;
	}
	else
		return false;
}


void Parser::parseWhiteSpace()
{
	for( ;notEOF() && ::isWhitespace(text[currentpos]); ++currentpos)
	{}
}


void Parser::parseSpacesAndTabs()
{
	for( ;notEOF() && (text[currentpos] == ' ' || text[currentpos] == '\t') ; ++currentpos)
	{}
}


void Parser::advancePastLine()
{
	for( ; !eof(); ++currentpos)
	{
		if(current() == '\n') // line feed (Unix)
		{
			currentpos++;
			return;
		}
		else if(current() == '\r') // carriage return
		{
			currentpos++;
			if(current() == '\n')
			{
				// Assume we are seeing a CRLF on windows.  Treat as one new line.
				currentpos++;
			}
			return;
		}
	}
}


// Advances until current() == target, or EOF is reached.
// Returns true iff EOF was not reached.
// returns in 'result_out' a string from current to target, not including target.
bool Parser::parseToChar(char target, string_view& result_out)
{
	const int initial_pos = currentpos;
	while(1)
	{
		if(eof())
			return false;
		if(current() == target)
		{
			result_out = string_view(text + initial_pos, currentpos - initial_pos);
			return true;
		}
		currentpos++;
	}
}


bool Parser::parseToOneOfChars(char target_a, char target_b, string_view& result_out)
{
	const int initial_pos = currentpos;
	while(1)
	{
		if(eof())
			return false;
		if(current() == target_a || current() == target_b)
		{
			result_out = string_view(text + initial_pos, currentpos - initial_pos);
			return true;
		}
		currentpos++;
	}
}


void Parser::parseToCharOrEOF(char target, string_view& result_out)
{
	const int initial_pos = currentpos;
	while(1)
	{
		if(eof() || current() == target)
		{
			result_out = string_view(text + initial_pos, currentpos - initial_pos);
			return;
		}
		currentpos++;
	}
}


void Parser::parseLine()
{
	for( ; notEOF() && current() != '\n' && current() != '\r'; ++currentpos)
	{
		currentpos++;
	}
}


char Parser::current() const
{
	assert(currentpos < textsize);
	return text[currentpos];
}


bool Parser::eof() const
{
	return currentpos >= textsize;
}


bool Parser::notEOF() const
{
	return currentpos < textsize;
}


void Parser::advance()
{
	currentpos++;
}
