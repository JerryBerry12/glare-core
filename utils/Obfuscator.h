/*=====================================================================
Obfuscator.h
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Tue May 25 18:32:39 +1200 2010
=====================================================================*/
#pragma once


#include <string>
#include <set>


/*=====================================================================
Obfuscator
-------------------

=====================================================================*/
class Obfuscator
{
public:
	Obfuscator(bool collapse_whitespace, bool remove_comments, bool change_tokens, bool cryptic_tokens);
	~Obfuscator();

	static const std::string tokenHashString(const std::string& t);
	const std::string mapToken(const std::string& t);

	const std::string obfuscate(const std::string& s);

	static void obfuscateKernels(const std::string& kernel_dir);

	static void test();
private:
	bool collapse_whitespace;
	bool remove_comments;
	bool change_tokens;
	bool cryptic_tokens;

	std::set<std::string> keywords;
};
