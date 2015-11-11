/*=====================================================================
Obfuscator.cpp
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Tue May 25 18:32:39 +1200 2010
=====================================================================*/
#include "Obfuscator.h"


#include "Parser.h"
#include <string>
#include "../indigo/globals.h"
#include "../utils/StringUtils.h"
#include "../utils/Exception.h"
#include "../utils/FileUtils.h"
#include <fstream>
#include "Transmungify.h"
#include "TestUtils.h"
#include <stdlib.h>
#include <wnt_LangParser.h>
#include <wnt_Lexer.h>
#include <wnt_Variable.h>
#include <wnt_FunctionExpression.h>
#include <xxhash.h>
#include <map>
#include <algorithm>


Obfuscator::Obfuscator(bool collapse_whitespace_, bool remove_comments_, bool change_tokens_, /*bool cryptic_tokens_, */Lang lang_)
:	collapse_whitespace(collapse_whitespace_),
	remove_comments(remove_comments_),
	change_tokens(change_tokens_),
	//cryptic_tokens(cryptic_tokens_),
	lang(lang_)
{
	if(lang == Lang_Winter)
	{
		addWinterKeywords();
	}
	else if(lang == Lang_OpenCL)
	{
		addOpenCLKeywords();
	}
	else
	{
		assert(0);
	}
}


Obfuscator::~Obfuscator()
{
}


void Obfuscator::addOpenCLKeywords()
{
	const char* keywords_[] = {

"__OPENCL_VERSION__",

// Preprocessor keywords
"if",
"elif",
"endif",
"ifdef",
"ifndef",
"define",
"defined",
"pragma",
"once",
"include",

// OpenCL C keywords
"auto",
"bool",
"break",
"case",
"char",
"uchar",
"const",
"continue",
"default",
"do",
"double",
"else",
"enum",
"extern",
"false",
"float",
"float4",
"float8",
"for",
"goto",
"if",
"int",
"int4",
"int8",
"long",
"register",
"return",
"short",
"signed",
"sizeof",
"static",
"struct",
"switch",
"true",
"typedef",
"union",
"unsigned",
"void",
"volatile",
"restrict",
"while",

"convert_int4",
"vload4",

// CUDA and OpenCL keywords

"as_int",
"float2",
"float3",
"float4",
"float8",
"float16",
"double2",
"double3",
"double4",
"double8",
"double16",
"int8",
"uint",
"uint4",
"texture",
"__device__",
"__constant__",
"__global__",
"__shared__",
"tex1Dfetch",
"__float_as_int",

"abs",
"acos", "acosh",
"asin", "asinh",
"atan", "atanh",
"atan2",
"cos", "cosh",
"exp",
"log",
"fabs",
"pow",
"rsqrt",
"sin", "sinh",
"tan", "tanh",
"sqrt",

"min",
"max",
"floor",
"ceil",
"clamp",

"printf",

"atomic_add",
"get_local_size",
"barrier",
"CLK_LOCAL_MEM_FENCE",
"isnan",


// float4 elements:
"x",
"y",
"z",
"w",

// vector elements:
"v",
"s0",
"s1",
"s2",
"s3",
"s4",
"s5",
"s6",
"s7",

// swizzles:
"xyz",

// OpenCL: see '6.1.9 Keywords' in OpenCL 1.1 spec.
"__global",
"global",
"__local",
"local",
"__constant",
"constant",
"__private",
"private",

"__kernel", 
"kernel",

"__read_only", "read_only", "__write_only", "write_only", 
"__read_write","read_write", 
 
"isfinite",

"sampler_t",
"image2d_t",
"read_imagef",
"write_imagef",
"int2",
"get_local_id",
"get_global_id",
"inline",
"atom_add",
"fmin",
"fmax",
"fmod",
"mix",
"as_uint4",
"as_float",
"as_uint",

"convert_int8",

"dot",
"cross",
"length",
"normalize",

"CLK_NORMALIZED_COORDS_FALSE", "CLK_ADDRESS_CLAMP_TO_EDGE", "CLK_FILTER_NEAREST",

//"ThreadData",
//"static_float_data", "dynamic_float_data", "qrng_data", "thread_data",

"lll11l11lll1", // "iter_depth",
"l1111l11l111", // "num_iterators",
"l11ll11l1ll1", // "pal_values",
"ll1l111lll11", // "dyn_floats",
"l11llllll1l1", // "initDynamicData",
"ll11ll1l111l", // "getCameraVectors",
"ll1lll1llll1", // "applyTransform",
"l1llll1lllll", // "applyCameraTransform",
"l1lll1ll1l11", // Zero kernel
"l1lllll111l1", // QRNG kernel
"ll1ll11l1ll1", // Main iteration kernel


		// Winter tuple field names
		"field_0",
		"field_1",
		"field_2",
		"field_3",


		// Don't obfuscate these types.  Otherwise we have to obfuscate the signatures of all the external functions (noise() etc..).  Which could be done but is a hassle.
		"vec2",
		"vec3",
		"vec4",
		"mat2x2",
		"mat3x3",
		"mat4x4",

		"FullHitInfo",
		"BasicMaterialData",
		"SpectralVector",
		"Colour3",
		"ScatterArgs",
		"ScatterResults",
		"EvaluateBSDFArgs",
		"EvalBSDFResults",

		// Functions from lang\winter_opencl_support_code_declarations.cl
		// TODO: remove from ignore list - obfuscate this stuff
		"noise_float__float_",
		"noise_float__float__float_",
		"noise4Valued_float__float_",
		"noise4Valued_float__float__float_",
		"fbm_float__float__int_",
		"fbm_float__float__float__int_",
		"fbm4Valued_float__float__int_",
		"fbm4Valued_float__float__float__int_",
		"gridNoise_float__float__float__float_",
		"voronoi_vec2__float_",
		"voronoi3d_vec3__float_",
		"mod_int__int_",
		"print_vec4_",

		/*
		"sampleTexDummy_int__vec2__FullHitInfo_",
		"sampleTexScalarDummy_int__vec2__FullHitInfo_",
		"evalTexDerivsForST_int__vec2__FullHitInfo_",
		"getTexCoords_int__FullHitInfo_",
		"getWorldToObMatrix_FullHitInfo_",
		"getObToWorldMatrixRows_FullHitInfo_",
		"getSample_int__int__int____global_array_float__28__",
		//"getSamplePair_int__int__int____global_array_float__28__",
		"evalTexDerivsForST_int__vec2__FullHitInfo_",*/
NULL
	};

	for(int i=0; keywords_[i]; i++)
	{
		//conPrint(keywords_[i]);
		this->keywords.insert(keywords_[i]);
	}
}


void Obfuscator::addWinterKeywords()
{
	const char* keywords_[] = {
		"main", // Not a keyword, but useful so that we can still find the entry-point function for tests etc..
		"testExternalFunc", // Not a keyword, but useful so that we can still find the entry-point function for tests etc..
		
		"op_lt",
		"op_gt",
		"op_eq",
		"op_neq",
		"op_lte",
		"op_gte",
		"op_add",
		"op_mul",
		"op_div",
		"op_sub",
		"op_unary_minus",

		// Special functions, taken from Linker.cpp
		"floor",
		"ceil",
		"sqrt",
		"sin",
		"cos",
		"exp",
		"log",
		"abs",
		"truncateToInt",
		"sign",
		"toFloat",
		"toInt",
		"length",
		"map",
		"inBounds",
		"pow",
		"shuffle",
		"min",
		"max",
		"dot",
		"makeVArray",
		"iterate",
		"fold",
		"update",
		"elem",

		"v",

		/*"sampleTexDummy",
		"sampleTexScalarDummy",
		"evalTexDerivsForST",
		"getTexCoords",
		"getWorldToObMatrix",
		"getObToWorldMatrixRows",
		"getSample",
		//"getSamplePair",*/

		"noise",
		"noise4Valued",
		"fbm",
		"fbm4Valued",
		"gridNoise",
		"voronoi",
		"voronoi3d",
		"mod",
		"print",

		// Don't obfuscate these types.  Otherwise we have to obfuscate the signatures of all the external functions (noise() etc..).  Which could be done but is a hassle.
		"vec2",
		"vec3",
		"vec4",
		"mat2x2",
		"mat3x3",
		"mat4x4",

		"FullHitInfo",
		"BasicMaterialData",
		"SpectralVector",
		"Colour3",

		"ScatterArgs",
		"ScatterResults",
		"EvaluateBSDFArgs",
		"EvalBSDFResults",

		// tuple field names
		"field_0",
		"field_1",
		"field_2",
		"field_3",

		NULL
	};

	for(int i=0; keywords_[i]; i++)
		this->keywords.insert(keywords_[i]);
}


const std::string Obfuscator::tokenHashString(const std::string& t)
{
	// Use hash of token to produce obfuscated token
	uint64 str_hash = XXH64(t.data(), t.size(), 
		3835675695284957659ULL // seed - just a somewhat random value.
	);

	//const std::string new_token = t + "_" + toString(str_hash); // Appends hash code to end of token, use this for debugging.
	
	std::string new_token;
	new_token.resize(33);
	new_token[0] = 'l';
	for(int i = 0; i < 32; ++i)
	{
		new_token[i + 1] = (str_hash & 1) ? 'l' : '1';
		str_hash >>= 1;
	}

	return new_token;
}


// Is the function name something like 'eXX'.  Taken from wnt_FunctionExpression.cpp
static bool isENFunctionName(const std::string& name)
{
	if(name.size() < 2)
		return false;
	if(name[0] != 'e')
		return false;
	for(size_t i=1; i<name.size(); ++i)
		if(!isNumeric(name[i]))
			return false;
	return true;
}


const std::string Obfuscator::mapToken(const std::string& t)
{
	if(!change_tokens)
		return t;

	if(this->keywords.find(t) != this->keywords.end())
		return t;

	if(lang == Lang_Winter)
	{
		// Don't obfuscate eN functions (elem access functions)
		if(isENFunctionName(t))
			return t;
	}

	if(::hasPrefix(t, "tuple")) // There are issues with obfuscation of tuple typenames, just don't obfuscate them.
		return t;

	return tokenHashString(t);
}


const std::string Obfuscator::obfuscate(const std::string& s)
{
	Parser p(s.c_str(), (unsigned int)s.length());

	std::string res;


	const std::string ignore_tokens = "f[](){}<>/*-+=;,.^&!|?:%";

	bool parsing_preprocessor_line = false; // Keep track of if we are parsing a preprocessor line, so we can put a newline after it.
	const int MAX_LINE_LEN = 200;
	size_t last_line_start_res_len = 0; // Break lines after a while even if we are collapsing whitespace.  Otherwise stuff like OpenCL warnings get extremely long

	while(p.notEOF())
	{
		string_view t;

		//const char debug_current_char = p.current();
		//conPrint(std::string(1, debug_current_char));

		//if((p.current() == 'x' && p.nextIsChar('<')) ||
		//	(p.current() == '>' && p.nextIsChar('>')))
		//{
		//	conPrint("found 'x  ='");
		//}

		if(p.parseString("//BEGIN_INCLUDES"))
		{
			// Skip lines until we get to the line with '//END_INCLUDES'.
			while(1)
			{
				p.advancePastLine();
				if(p.eof())
					throw Indigo::Exception("End of file while looking for //END_INCLUDES");
				if(p.parseString("//END_INCLUDES"))
				{
					p.advancePastLine();
					break;
				}
			}
		}
		if(p.current() == '/' && p.nextIsChar('/')) // C++-style line comment (//)
		{
			const int initial_pos = p.currentPos();

			p.advancePastLine();

			if(remove_comments)
			{
				// We need a newline at the end of lines like    #if 0//(BLOCK_SIZE <= 32)
				if(parsing_preprocessor_line)
					res.push_back('\n');
			}
			else
				res += s.substr(initial_pos, p.currentPos() - initial_pos); // append comment to output
			//else if(res.substr(res.size() - 1, 1) != "\n")
			//	res += "\n";
			continue;
		}
		else if(p.current() == '#') // preprocessor def
		{
			parsing_preprocessor_line = true;
			p.advance();
			if(collapse_whitespace) // If we are collapsing whitespace, the newline before this line may have been removed.  So add a newline in.
				res.push_back('\n');
			res.push_back('#');
			continue;
		}
		/*else if(p.current() == '#') // preprocessor def
		{
			int last_currentpos = p.currentPos();

			if(s.substr(last_currentpos, 7) == "#define")
			{
				// skip past the "#define " string to token
				for(int z = 0; z < 8; ++z)
					p.advance();

				// Parse the token
				bool valid_token_parse = p.parseIdentifier(t);
				if(valid_token_parse)
					conPrint("found #define " + t.to_string());

				// Make sure the output has a newline at the end (defines have to be on their own line I think)
				if(res.substr(res.size() - 1, 1) != "\n")
					res += "\r\n";

				// Output the (modified) #define
				res += "#define " + mapToken(t.to_string());

				// Read in the the definition of the define:
				int last_currentpos = p.currentPos();
				p.advancePastLine(); // Go past line with #define in it.

				// Output the rest of the define line
				res += s.substr(last_currentpos, p.currentPos() - last_currentpos);
				continue;
			}

			p.advancePastLine();
			if(res.substr(res.size() - 1, 1) != "\n")
				res += "\r\n";
			res += s.substr(last_currentpos, p.currentPos() - last_currentpos);
			continue;
		}*/
		else if(p.current() == '"') // string
		{
			int last_currentpos = p.currentPos();
			p.parseChar('"');

			while(p.notEOF() && p.current() != '"')
			{
				p.advance();
			}

			p.parseChar('"');

			res += s.substr(last_currentpos, p.currentPos() - last_currentpos);

			continue;
		}
		else if(p.current() == '/' && p.nextIsChar('*'))
		{
			int last_currentpos = p.currentPos();
			p.parseChar('/');
			p.parseChar('*');

			while(p.notEOF() && !(p.current() == '*' && p.nextIsChar('/')))
			{
				p.advance();
			}
			p.parseChar('*');
			p.parseChar('/');
			if(!remove_comments)
				res += s.substr(last_currentpos, p.currentPos() - last_currentpos); // append comment to output
			continue;
		}
		else if(::isWhitespace(p.current())) // Note that if we are removing whitespace and not removing comments, then newlines at end of comments can give incorrect results
		{
			int last_currentpos = p.currentPos();
			bool parsed_newline = false;
			for( ;p.notEOF() && ::isWhitespace(p.getText()[p.currentPos()]); p.advance())
			{
				if(p.current() == '\n' || p.current() == '\r')
					parsed_newline = true;
			}

			if(collapse_whitespace && !parsing_preprocessor_line)
				res += " ";
			else
				res += s.substr(last_currentpos, p.currentPos() - last_currentpos);

			if(parsed_newline)
				parsing_preprocessor_line = false;

			// Break lines after a while even if we are collapsing whitespace.  Otherwise stuff like OpenCL warnings get extremely long.
			if(collapse_whitespace && parsed_newline && (res.size() - last_line_start_res_len) > MAX_LINE_LEN)
			{
				res += "\n";
				last_line_start_res_len = res.size();
			}

			continue;
		}
		else if(p.current() == '0' && p.next() == 'x') // Hex number
		{
			const int num_hex_chars = 10 + 6 + 1; // 0-9, a-e and x
			const char hex_chars[num_hex_chars] =
			{
				'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
				'a', 'b', 'c', 'd', 'e', 'f',
				'x'
			};

			int last_currentpos = p.currentPos();

			while(p.notEOF())
			{
				const char current_char = toLowerCase(p.current());

				bool found_hex_char = false;
				for(int z = 0; z < num_hex_chars; ++z)
					if(current_char == hex_chars[z])
					{
						found_hex_char = true;
						break;
					}

				if(!found_hex_char) break;
				else p.advance();
			}

			res += s.substr(last_currentpos, p.currentPos() - last_currentpos);
			continue;
		}
		else if(p.parseIdentifier(t))
		{
			res += mapToken(t.to_string());
			continue;
		}
		else if(p.fractionalNumberNext())
		{
			int last_currentpos = p.currentPos();
			double val;
			p.parseDouble(val);
			res += s.substr(last_currentpos, p.currentPos() - last_currentpos);
			continue;
		}
		else if(::isNumeric(p.current()))
		{
			int last_currentpos = p.currentPos();

			int val;
			const bool valid_int_parse = p.parseInt(val);
			if(!valid_int_parse)
			{
				// Try parsing as an unsigned int
				uint32 uint_val;
				const bool valid_uint_parse = p.parseUnsignedInt(uint_val);

				// Fail, might need to try 64bit integer
				if(!valid_uint_parse)
					throw Indigo::Exception("Int parse failed at start of '" + s.substr(p.currentPos(), 10) + "'");
			}

			res += s.substr(last_currentpos, p.currentPos() - last_currentpos);
			continue;
		}

		/*if(p.parseInt())
		{
			int last_currentpos = p.currentPos();
			p.parseDouble();
			res += s.substr(last_current, p.currentPos() - last_currentpos);
		}*/

		{
			bool found = false;
			for(size_t i=0; i<ignore_tokens.size() && !found; ++i)
			{
				if(p.parseChar(ignore_tokens[i]))
				{
					res += ignore_tokens[i];
					found = true;
				}
			}
			if(found)
				continue;
		}

		//if(!processed)
		//{
			throw Indigo::Exception("Unhandled character at start of '" + s.substr(p.currentPos(), 10) + "'");
		//}
	}

	return res;
}


class RenamerVisitor : public Winter::ASTNodeVisitor
{
public:
	virtual void visit(Winter::ASTNode& node, Winter::TraversalPayload& payload)
	{
		switch(node.nodeType())
		{
		case Winter::ASTNode::VariableASTNodeType:
			{
				Winter::Variable* var = static_cast<Winter::Variable*>(&node);

				const std::string old_name = var->name;
				var->name = obfuscator->mapToken(old_name);
				//conPrint("mapped var " + old_name + " to " + var->name);
				break;
			}
		case Winter::ASTNode::FunctionExpressionType:
			{
				Winter::FunctionExpression* func_expr = static_cast<Winter::FunctionExpression*>(&node);

				if(!func_expr->static_function_name.empty())
				{
					const std::string old_name = func_expr->static_function_name;
					func_expr->static_function_name = obfuscator->mapToken(old_name);
					//conPrint("mapped func_expr static function name " + old_name + " to " + func_expr->static_function_name);
				}
				break;
			}
		case Winter::ASTNode::FunctionDefinitionType:
			{
				Winter::FunctionDefinition* def = static_cast<Winter::FunctionDefinition*>(&node);

				const std::string old_name = def->sig.name;
				def->sig.name = obfuscator->mapToken(old_name);
				//conPrint("mapped def name " + old_name + " to " + def->sig.name);

				// Obfuscate func arg names
				for(size_t i=0; i<def->args.size(); ++i)
				{
					def->args[i].name = obfuscator->mapToken(def->args[i].name);
				}

				break;
			}
		case Winter::ASTNode::LetType:
			{
				Winter::LetASTNode* let_node = static_cast<Winter::LetASTNode*>(&node);

				// Obfuscate let var names
				for(size_t i=0; i<let_node->vars.size(); ++i)
				{
					const std::string old_name = let_node->vars[i].name;
					let_node->vars[i].name = obfuscator->mapToken(old_name);
					//conPrint("mapped let var name " + old_name + " to " + let_node->vars[i].name);
				}

				break;
			}
		case Winter::ASTNode::NamedConstantType:
			{
				Winter::NamedConstant* named_const = static_cast<Winter::NamedConstant*>(&node);

				const std::string old_name = named_const->name;
				named_const->name = obfuscator->mapToken(old_name);
				//conPrint("mapped named constant " + old_name + " to " + named_const->name);

				break;
			}
		};
	}

	Obfuscator* obfuscator;
};


const std::string Obfuscator::obfuscateWinterSource(const std::string& src)
{
	try
	{
		Winter::SourceBufferRef source_buffer = new Winter::SourceBuffer("source", src);
		std::vector<Reference<Winter::TokenBase> > tokens;
		Winter::Lexer::process(source_buffer, tokens);

		Winter::LangParser lang_parser(
			false, // floating_point_literals_default_to_double
			false // real_is_double
		);

		std::map<std::string, Winter::TypeRef> named_types;
		std::vector<Winter::TypeRef> named_types_ordered;
		int function_order_num = 0;

		Reference<Winter::BufferRoot> buffer_root = lang_parser.parseBuffer(tokens,
			source_buffer,
			named_types,
			named_types_ordered,
			function_order_num
		);

		{
			std::vector<Winter::ASTNode*> stack;
			Winter::TraversalPayload payload(Winter::TraversalPayload::CustomVisit);
			Reference<RenamerVisitor> visitor = new RenamerVisitor();
			visitor->obfuscator = this;
			payload.custom_visitor = visitor;
			for(size_t i=0; i<buffer_root->top_level_defs.size(); ++i)
				buffer_root->top_level_defs[i]->traverse(payload, stack);
		}

		// Obfuscate structure definitions (structure and field names)
		for(size_t i=0; i<named_types_ordered.size(); ++i)
		{
			Winter::TypeRef named_type = named_types_ordered[i];
			switch(named_type->getType())
			{
			case Winter::Type::StructureTypeType:
				{
				Winter::StructureType* struct_type = named_type.downcastToPtr<Winter::StructureType>();

				struct_type->name = this->mapToken(struct_type->name);

				for(size_t z=0; z<struct_type->component_names.size(); ++z)
					struct_type->component_names[z] = this->mapToken(struct_type->component_names[z]);

				break;
				}
			};
		}



		std::string output;

		// Re-serialise structure definitions
		for(size_t i=0; i<named_types_ordered.size(); ++i)
		{
			Winter::TypeRef named_type = named_types_ordered[i];
			switch(named_type->getType())
			{
			case Winter::Type::StructureTypeType:
				output += named_type.downcastToPtr<Winter::StructureType>()->definitionString();
				output += "\n";
				break;
			};
		}

		// Re-serialise functions
		for(size_t i=0; i<buffer_root->top_level_defs.size(); ++i)
		{
			Winter::ASTNodeRef node = buffer_root->top_level_defs[i];
			switch(node->nodeType())
			{
			case Winter::ASTNode::FunctionDefinitionType:
				{
					if(node.downcastToPtr<Winter::FunctionDefinition>()->body.nonNull()) // If not a built-in function:
					{
						std::string s = buffer_root->top_level_defs[i]->sourceString();
						output += s;
						output += "\n\n";
					}
					break;
				}
			default:
				{
					std::string s = buffer_root->top_level_defs[i]->sourceString();
					output += s;
					output += "\n\n";
					break;
				}
			};
		}
		return output;
	}
	catch(Winter::BaseException& e)
	{
		throw Indigo::Exception(e.what());
	}
}


const std::string Obfuscator::encryptedFilename(const std::string& plaintext_filename)
{
	const uint64 hash = XXH64(plaintext_filename.c_str(), plaintext_filename.length(), 456765836);
	//return plaintext_filename + "_" + toHexString(hash);//plaintext_filename.substr(0, 2) + toHexString(hash); // Can use this for debugging.
	return toHexString(hash);
}


const std::string Obfuscator::readAndDecryptFile(const std::string& path)
{
	try
	{
		std::string cyphertext;
		FileUtils::readEntireFile(path, cyphertext);

		if(cyphertext.size() % 4 != 0)
			throw Indigo::Exception("invalid file length for file '" + path + "'");

		std::string plaintext;
		Transmungify::decrypt((const uint32*)cyphertext.data(), (uint32)cyphertext.size() / 4, plaintext);
		return plaintext;
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw Indigo::Exception(e.what());
	}
}


const std::string Obfuscator::readFileFromDisk(const std::string& indigo_base_dir, const std::string& plain_text_path)
{
	try
	{
		if(useObfuscatedSource())
		{
			const std::string filename = FileUtils::getFilename(plain_text_path);

			// Read from indigo_base_dir/data/obs
			const std::string dir = indigo_base_dir + "/data/obs";

			const std::string encypted_filename = encryptedFilename(filename);

			const std::string encryped_path = dir + "/" + encypted_filename;

			conPrint("reading encrypted source from '" + encryped_path + "'...");

			return readAndDecryptFile(encryped_path);
		}
		else
		{
			// Assume that all plain-text files we are trying to read are in INDIGO_TRUNK/lang if not specified otherwise
			const std::string full_path = FileUtils::join(TestUtils::getIndigoTestReposDir() + "/lang", plain_text_path);

			return FileUtils::readEntireFileTextMode(full_path);
		}
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw Indigo::Exception(e.what());
	}
}


// Change their obfuscated names so that they will be the same as the obfuscated names generated from winter code.
const std::string Obfuscator::renameOpenCLSupportFunctions(const std::string& support_opencl_code)
{
	if(!Obfuscator::useObfuscatedSource())
		return support_opencl_code;

	std::string code = support_opencl_code;

	const char* replacements[] = {
		"sampleTexDummy_int__vec2__FullHitInfo_",
		"sampleTexScalarDummy_int__vec2__FullHitInfo_",
		"evalTexDerivsForST_int__vec2__FullHitInfo_",
		"getTexCoords_int__FullHitInfo_",
		"getWorldToObMatrix_FullHitInfo_",
		"getObToWorldMatrixRows_FullHitInfo_",
		"getSample_int__int__int____global_array_float__28__",
		"getSamplePair_int__int__int____global_array_float__28__",

		/*"noise_float__float_",
		"noise_float__float__float_",
		"noise4Valued_float__float_",
		"noise4Valued_float__float__float_",
		"fbm_float__float__int_",
		"fbm_float__float__float__int_",
		"fbm4Valued_float__float__int_",
		"fbm4Valued_float__float__float__int_",
		"gridNoise_float__float__float__float_",
		"voronoi_vec2__float_",
		"voronoi3d_vec3__float_",
		"mod_int__int_",
		"print_vec4_",*/
	};

	for(size_t i=0; i<sizeof(replacements)/sizeof(const char*); ++i)
	{
		const std::string func_name = ::getPrefixBeforeDelim(replacements[i], '_');
		const std::string type_decoration = "_" + ::getSuffixAfterDelim(replacements[i], '_');
		code = StringUtils::replaceAll(code,
			Obfuscator::hashIfObfuscating(replacements[i]),
			Obfuscator::hashIfObfuscating(func_name) + type_decoration);
	}

	return code;
}


void Obfuscator::obfuscateKernels(const std::string& kernel_dir)
{
	Obfuscator win_ob(
		true, // collapse_whitespace
		true, // remove_comments
		true, // change tokens
		Obfuscator::Lang_Winter
	);

	Obfuscator opencl_ob(
		true, // collapse_whitespace
		true, // remove_comments
		true, // change tokens
		Obfuscator::Lang_OpenCL
	);

	const bool encrypt = true;

	try
	{
		//==================== Obfuscate material (and material related) source code ==================
		std::set<std::string> files;

		files.insert("ISL_stdlib.txt");
		files.insert("material.win");

		files.insert("winter_opencl_support_code_declarations.cl");
		files.insert("winter_opencl_support_code.cl");

		files.insert("compute_shading_normal.win");

		files.insert("basic_diffuse.win");
		files.insert("basic_phong.win");
		files.insert("basic_specular.win");

		files.insert("blend.win");
		files.insert("coating.win");
		files.insert("diffuse.win");
		files.insert("phong.win");
		files.insert("specular.win");
		files.insert("null_material.win");
		files.insert("sky_material.win");

		for(auto i = files.begin(); i != files.end(); ++i)
		{
			const std::string WINTER_SRC_READ_DIR = TestUtils::getIndigoTestReposDir() + "/lang";

			const std::string src_path = WINTER_SRC_READ_DIR + "/" + *i;

			std::string src;
			FileUtils::readEntireFileTextMode(src_path, src);

			std::string ob_src;
			if(::hasExtension(src_path, "win") || ::hasExtension(src_path, "txt"))
			{
				// Obfuscate the code
				ob_src = win_ob.obfuscateWinterSource(src);
			}
			else if(::hasExtension(src_path, "cl"))
			{
				ob_src = opencl_ob.obfuscate(src);
			}
			else
			{
				assert(0);
			}

		
			const std::string outpath = "data/obs/" + encryptedFilename(FileUtils::getFilename(src_path));

			if(encrypt)
			{
				// Encrypt/transmungify source
				std::vector<uint32> cyphertext;
				Transmungify::encrypt(ob_src, cyphertext);
				FileUtils::writeEntireFile(outpath, (const char*)cyphertext.data(), cyphertext.size()*sizeof(uint32));
			}
			else
				FileUtils::writeEntireFileTextMode(outpath, ob_src);

			conPrint("Encrypted/obfuscated '" + src_path + "' to '" + outpath + "'");	
		}
		

		//==================== Obfuscate OpenCL path tracer kernels ==================
		{
			const int num_files = 10;
			std::string files[num_files] =
			{
				// Include files. Note that the order here matters, because we can't use #include statements and #pragma once etc
				"OpenCLPathTracingKernel.h",
				"ptkernel_spectral.h",
				"ptkernel_bvh.h",
				"ptkernel_maths.h",
				"ptkernel_samplingutils.h",
				"ptkernel_samplingutils_winter.h",
				"ptkernel_textures.h",
				"ptkernel_textures_winter.h",
				"ptkernel_imaging.h",

				// Main kernel file
				"OpenCLPathTracingMicroKernels.cl"
			};

			std::string complete_kernel_string;
			for(int z = 0; z < num_files; ++z)
			{
				std::string s;
				FileUtils::readEntireFileTextMode(FileUtils::join(kernel_dir, files[z]), s);

				complete_kernel_string += s + "\n";
			}

			// Obfuscate the code
			std::string ob_src = opencl_ob.obfuscate(complete_kernel_string);

			ob_src = renameOpenCLSupportFunctions(ob_src);


			const std::string outpath = "data/OPT";

			if(encrypt)
			{
				// Encrypt/transmungify source
				std::vector<uint32> cyphertext;
				Transmungify::encrypt(ob_src, cyphertext);
				FileUtils::writeEntireFile(outpath, (const char*)cyphertext.data(), cyphertext.size()*sizeof(uint32));
			}
			else
				FileUtils::writeEntireFileTextMode(outpath, ob_src);

			conPrint("Wrote '" + outpath + "'");	
		}


		// Obfuscate MaterialScatterMicroKernel.cl to data/MSMK
		{
			std::string complete_kernel_string;
			FileUtils::readEntireFileTextMode(FileUtils::join(kernel_dir, "MaterialScatterMicroKernel.cl"), complete_kernel_string);

			// Obfuscate the code
			const std::string ob_src = opencl_ob.obfuscate(complete_kernel_string);

			const std::string outpath = "data/MSMK";

			if(encrypt)
			{
				// Encrypt/transmungify source
				std::vector<uint32> cyphertext;
				Transmungify::encrypt(ob_src, cyphertext);
				FileUtils::writeEntireFile(outpath, (const char*)cyphertext.data(), cyphertext.size()*sizeof(uint32));
			}
			else
				FileUtils::writeEntireFileTextMode(outpath, ob_src);

			conPrint("Wrote '" + outpath + "'");	
		}
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Error: " + e.what());
		exit(1);
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		conPrint("Error: " + e.what());
		exit(1);
	}
}


#if BUILD_TESTS


void Obfuscator::test()
{
	try
	{
		Obfuscator ob(
			true, // collapse_whitespace
			true, // remove_comments
			true, // change tokens
			Obfuscator::Lang_OpenCL
		);

		// Make sure newlines are removed if no preprocessor defs are around
		{
			const std::string s = 
"	int a = 0;			\n\
	int b = 1;			\n\
	int c = 2;			\n\
";
			const std::string ob_s = ob.obfuscate(s);
			conPrint("--------------");
			conPrint(ob_s);
			conPrint("--------------");
			testAssert(std::count(ob_s.begin(), ob_s.end(), '\n') == 0);
		}

		// Make sure newlines are removed if no preprocessor defs are around, and in the presence of comments
		{
			const std::string s = 
"	// a comment		\n\
	int a = 0;			\n\
	int b = 1;			\n\
	int c = 2;			\n\
";
			const std::string ob_s = ob.obfuscate(s);
			conPrint("--------------");
			conPrint(ob_s);
			conPrint("--------------");
			testAssert(std::count(ob_s.begin(), ob_s.end(), '\n') == 0);
		}

		// Make sure newlines at the end of preprocessor definitions are not removed.
		{
			const std::string s = 
"#if ENV_EMITTER\n\
	int a = 0;	\n\
#endif\n\
	int b = 1;";
	
			const std::string ob_s = ob.obfuscate(s);
			conPrint("--------------");
			conPrint(ob_s);
			conPrint("--------------");
			testAssert(std::count(ob_s.begin(), ob_s.end(), '\n') == 3 || std::count(ob_s.begin(), ob_s.end(), '\n') == 4);
		}

// Make sure newlines at the end of preprocessor definitions (with comments after them) are not removed.
		{
			const std::string s = 
"#if 0//(BLOCK_SIZE <= 32)		\n\
	return decodeMortonBlock5bit(morton);		\n\
#else		\n\
	return decodeMortonBlock16bit(morton);		\n\
#endif		\n\
";
			const std::string ob_s = ob.obfuscate(s);
			conPrint("--------------");
			conPrint(ob_s);
			conPrint("--------------");
			testAssert(std::count(ob_s.begin(), ob_s.end(), '\n') >= 6);

			const std::vector<std::string> lines = ::split(ob_s, '\n');
			int i = 0;
			testAssert(lines[i++] == "");
			testAssert(lines[i++] == "#if 0");
		}

	}
	catch(Indigo::Exception& e)
	{
		failTest("Error: " + e.what());
	}
}


#endif // BUILD_TESTS
