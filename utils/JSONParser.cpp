/*=====================================================================
JSONParser.cpp
-------------------
Copyright Glare Technologies Limited 2018 -
Generated at 2018-03-05 01:34:58 +1300
=====================================================================*/
#include "JSONParser.h"


#include "MemMappedFile.h"
#include "Parser.h"
#include "Exception.h"
#include "UTF8Utils.h"


const std::string& JSONNode::getStringValue() const
{
	if(type == JSONNode::Type_String)
		return string_v;
	else
		throw Indigo::Exception("Expected type String.");
}


size_t JSONNode::getUIntValue() const
{
	if(type == JSONNode::Type_Number)
		return (size_t)this->value.double_v;
	else
		throw Indigo::Exception("Expected type Number.");
}


double JSONNode::getDoubleValue() const
{
	if(type == JSONNode::Type_Number)
		return this->value.double_v;
	else
		throw Indigo::Exception("Expected type Number.");
}


bool JSONNode::hasChild(JSONParser& parser, const string_view& name) const
{
	if(type != JSONNode::Type_Object)
		throw Indigo::Exception("Expected type object.");

	for(size_t i=0; i<name_val_pairs.size(); ++i)
		if(name_val_pairs[i].name == name)
			return true;
	
	return false;
}


size_t JSONNode::getChildUIntValue(JSONParser& parser, const string_view& name) const
{
	if(type != JSONNode::Type_Object)
		throw Indigo::Exception("Expected type object.");

	for(size_t i=0; i<name_val_pairs.size(); ++i)
		if(name_val_pairs[i].name == name)
			return parser.nodes[name_val_pairs[i].value_node_index].getUIntValue();

	throw Indigo::Exception("Failed to find child name/value pair with name " + name + ".");
}


size_t JSONNode::getChildUIntValueWithDefaultVal(JSONParser& parser, const string_view& name, size_t default_val) const
{
	if(type != JSONNode::Type_Object)
		throw Indigo::Exception("Expected type object.");

	for(size_t i=0; i<name_val_pairs.size(); ++i)
		if(name_val_pairs[i].name == name)
			return parser.nodes[name_val_pairs[i].value_node_index].getUIntValue();

	return default_val;
}


const std::string& JSONNode::getChildStringValue(JSONParser& parser, const string_view& name) const
{
	if(type != JSONNode::Type_Object)
		throw Indigo::Exception("Expected type object.");

	for(size_t i=0; i<name_val_pairs.size(); ++i)
		if(name_val_pairs[i].name == name)
			return parser.nodes[name_val_pairs[i].value_node_index].getStringValue();

	throw Indigo::Exception("Failed to find child name/value pair with name " + name + ".");
}


const std::string JSONNode::getChildStringValueWithDefaultVal(JSONParser& parser, const string_view& name, const string_view& default_val) const
{
	if(type != JSONNode::Type_Object)
		throw Indigo::Exception("Expected type object.");

	for(size_t i=0; i<name_val_pairs.size(); ++i)
		if(name_val_pairs[i].name == name)
			return parser.nodes[name_val_pairs[i].value_node_index].getStringValue();

	return default_val.to_string();
}


const JSONNode& JSONNode::getChildObject(JSONParser& parser, const string_view& name) const
{
	if(type != JSONNode::Type_Object)
		throw Indigo::Exception("Expected type object.");

	for(size_t i=0; i<name_val_pairs.size(); ++i)
		if(name_val_pairs[i].name == name)
		{
			const JSONNode& child = parser.nodes[name_val_pairs[i].value_node_index];
			if(child.type != JSONNode::Type_Object)
				throw Indigo::Exception("Expected child to have type object.");
			return child;
		}

	throw Indigo::Exception("Failed to find child name/value pair with name " + name + ".");
}


const JSONNode& JSONNode::getChildArray(JSONParser& parser, const string_view& name) const
{
	if(type != JSONNode::Type_Object)
		throw Indigo::Exception("Expected type object.");

	for(size_t i=0; i<name_val_pairs.size(); ++i)
		if(name_val_pairs[i].name == name)
		{
			const JSONNode& child = parser.nodes[name_val_pairs[i].value_node_index];
			if(child.type != JSONNode::Type_Array)
				throw Indigo::Exception("Expected child to have type Array.");
			return child;
		}

	throw Indigo::Exception("Failed to find child name/value pair with name " + name + ".");
}


JSONParser::JSONParser()
{}


JSONParser::~JSONParser()
{}


std::string JSONParser::parseString(Parser& p)
{
	std::string s;

	// Parse opening "
	if(!p.parseChar('"'))
		throw Indigo::Exception("Expected \"" + errorContext(p));
	
	while(p.notEOF() && p.current() != '"')
	{
		if(p.current() == '\\') // Parse escape sequence:
		{
			p.consume('\\');
			if(p.eof())
				throw Indigo::Exception("EOF in escape sequence." + errorContext(p));
			switch(p.current())
			{
			case '"':
				s.push_back('"');
				p.advance();
				break;
			case '\\':
				s.push_back('\\');
				p.advance();
				break;
			case '/':
				s.push_back('/');
				p.advance();
				break;
			case 'b':
				s.push_back('\b');
				p.advance();
				break;
			case 'f':
				s.push_back('\f');
				p.advance();
				break;
			case 'n':
				s.push_back('\n');
				p.advance();
				break;
			case 'r':
				s.push_back('\r');
				p.advance();
				break;
			case 't':
				s.push_back('\t');
				p.advance();
				break;
			case 'u':
			{
				p.consume('u');
				if(p.currentPos() + 4 > p.getTextSize())
					throw Indigo::Exception("EOF while parsing unicode code point.." + errorContext(p));

				// Parse 4-hex-digit unicode code point
				uint32 code_point = 0;
				int bit_offset = 12;
				for(int i=0; i<4; ++i)
				{
					const char c = p.current();
					try
					{
						code_point |= hexCharToUInt(c) << bit_offset;
					}
					catch(StringUtilsExcep& e)
					{
						throw Indigo::Exception("Error while parsing unicode code point: " + e.what() + errorContext(p));
					}
					p.advance();
					bit_offset -= 4;
				}

				s += UTF8Utils::encodeCodePoint(code_point); // Encode code point as UTF-8 and append
				break;
			}
			default:
				throw Indigo::Exception("Invalid escape sequence." + errorContext(p));
			}
		}
		else
		{
			s.push_back(p.current());
			p.advance();
		}
	}

	// Parse closing "
	if(!p.parseChar('"'))
		throw Indigo::Exception("Expected \"" + errorContext(p));

	return s;
}


uint32 JSONParser::parseNode(Parser& parser)
{
	switch(parser.current())
	{
	case '{':
		return parseObject(parser);
	case '[':
		return parseArray(parser);
	case '"':
		return parseStringNode(parser);
	case 't':
		return parseTrue(parser);
	case 'f':
		return parseFalse(parser);
	case 'n':
		return parseNull(parser);
	case '-':
		return parseNumber(parser);
	default:
		if(::isNumeric(parser.current()))
			return parseNumber(parser);
		else
			throw Indigo::Exception("Unexpected character '" + std::string(1, parser.current()) + "'" + errorContext(parser));
	}
}


uint32 JSONParser::parseStringNode(Parser& p)
{
	const uint32 node_index = (uint32)nodes.size();
	nodes.push_back(JSONNode());
	nodes[node_index].type = JSONNode::Type_String;
	nodes[node_index].string_v = parseString(p);
	return node_index;
}


uint32 JSONParser::parseTrue(Parser& p)
{
	if(!p.parseCString("true"))
		throw Indigo::Exception("Expected 'true'" + errorContext(p));

	const uint32 node_index = (uint32)nodes.size();
	nodes.push_back(JSONNode());
	nodes[node_index].type = JSONNode::Type_Boolean;
	nodes[node_index].value.bool_v = true;
	return node_index;
}


uint32 JSONParser::parseFalse(Parser& p)
{
	if(!p.parseCString("false"))
		throw Indigo::Exception("Expected 'false'" + errorContext(p));

	const uint32 node_index = (uint32)nodes.size();
	nodes.push_back(JSONNode());
	nodes[node_index].type = JSONNode::Type_Boolean;
	nodes[node_index].value.bool_v = false;
	return node_index;
}


uint32 JSONParser::parseNull(Parser& p)
{
	if(!p.parseCString("null"))
		throw Indigo::Exception("Expected 'null'" + errorContext(p));

	const uint32 node_index = (uint32)nodes.size();
	nodes.push_back(JSONNode());
	nodes[node_index].type = JSONNode::Type_Null;
	return node_index;
}


uint32 JSONParser::parseNumber(Parser& p)
{
	const uint32 node_index = (uint32)nodes.size();
	nodes.push_back(JSONNode());
	nodes[node_index].type = JSONNode::Type_Number;

	if(!p.parseDouble(nodes[node_index].value.double_v))
		throw Indigo::Exception("Failed parsing number." + errorContext(p));
	
	return node_index;
}


uint32 JSONParser::parseArray(Parser& p)
{
	uint32 node_index = (uint32)nodes.size();
	nodes.push_back(JSONNode());
	nodes[node_index].type = JSONNode::Type_Array;

	p.consume('[');
	p.parseWhiteSpace();

	while(!p.currentIsChar(']'))
	{
		// Parse name string
		nodes[node_index].child_indices.push_back(parseNode(p));

		p.parseWhiteSpace();

		if(p.currentIsChar(','))
		{
			p.advance();
			p.parseWhiteSpace();
		}
		else
			break;
	}

	if(!p.parseChar(']'))
		throw Indigo::Exception("Expected }" + errorContext(p));

	return node_index;
}


uint32 JSONParser::parseObject(Parser& p)
{
	uint32 node_index = (uint32)nodes.size();
	nodes.push_back(JSONNode());
	nodes[node_index].type = JSONNode::Type_Object;

	p.consume('{');
	p.parseWhiteSpace();

	while(!p.currentIsChar('}'))
	{
		// Parse name string
		nodes[node_index].name_val_pairs.push_back(JSONNameValuePair());
		nodes[node_index].name_val_pairs.back().name = parseString(p);

		p.parseWhiteSpace();
		if(!p.parseChar(':'))
			throw Indigo::Exception("Expected :" + errorContext(p));
		p.parseWhiteSpace();

		nodes[node_index].name_val_pairs.back().value_node_index = parseNode(p);

		p.parseWhiteSpace();
		if(p.currentIsChar(','))
		{
			p.advance();
			p.parseWhiteSpace();
		}
		else
			break;
	}

	if(!p.parseChar('}'))
		throw Indigo::Exception("Expected }" + errorContext(p));

	return node_index;
}


void JSONParser::parseBuffer(const char* data, size_t size)
{
	Parser parser(data, (unsigned int)size);

	parseNode(parser);
}


void JSONParser::parseFile(const std::string& path)
{
	MemMappedFile file(path);

	parseBuffer((const char*)file.fileData(), file.fileSize());
}


std::string JSONParser::errorContext(Parser& p)
{
	const std::string buf(p.getText(), p.getText() + p.getTextSize());

	size_t line_num, col;
	StringUtils::getPosition(buf, p.currentPos(), line_num, col);

	const std::string line = StringUtils::getLineFromBuffer(buf, p.currentPos());

	std::string res = "\nline " + toString(line_num + 1) + ", col " + toString(col) + "\n";
	res += line;
	res += "\n";
	for(size_t z=0; z<col; ++z)
		res.push_back('-');
	res.push_back('^');
	return res;
}


#if BUILD_TESTS


#include "../indigo/TestUtils.h"


static void testStringEscapeSequence(const std::string& encoded_string, const std::string& target_decoding)
{
	try
	{
		JSONParser p;
		std::string s = "{ \"the string\": \"" + encoded_string + "\" }";
		p.parseBuffer(s.data(), s.size());

		JSONNode root_ob = p.nodes[0];
		testAssert(root_ob.type == JSONNode::Type_Object);
		testAssert(root_ob.name_val_pairs.size() == 1);

		testAssert(root_ob.name_val_pairs[0].name == "the string");
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].type == JSONNode::Type_String);
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].string_v == target_decoding);
	}
	catch(Indigo::Exception& e)
	{
		failTest(e.what());
	}
}


static void testInvalidStringEscapeSequence(const std::string& encoded_string)
{
	try
	{
		JSONParser p;
		std::string s = "{ \"the string\": \"" + encoded_string + "\" }";
		p.parseBuffer(s.data(), s.size());
		failTest("Expected exception to be thrown.");
	}
	catch(Indigo::Exception&)
	{
	}
}


void JSONParser::test()
{
	try
	{
		JSONParser p;
		p.parseFile(TestUtils::getIndigoTestReposDir() + "/testfiles/json/example.json");

		JSONNode root_ob = p.nodes[0];
		testAssert(root_ob.type == JSONNode::Type_Object);
		testAssert(root_ob.name_val_pairs.size() == 8);

		testAssert(root_ob.name_val_pairs[0].name == "firstName");
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].type == JSONNode::Type_String);
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].string_v == "John");

		testAssert(root_ob.name_val_pairs[1].name == "lastName");
		testAssert(p.nodes[root_ob.name_val_pairs[1].value_node_index].type == JSONNode::Type_String);
		testAssert(p.nodes[root_ob.name_val_pairs[1].value_node_index].string_v == "Smith");

		testAssert(root_ob.name_val_pairs[2].name == "isAlive");
		testAssert(p.nodes[root_ob.name_val_pairs[2].value_node_index].type == JSONNode::Type_Boolean);
		testAssert(p.nodes[root_ob.name_val_pairs[2].value_node_index].value.bool_v == true);

		testAssert(root_ob.name_val_pairs[3].name == "age");
		testAssert(p.nodes[root_ob.name_val_pairs[3].value_node_index].type == JSONNode::Type_Number);
		testAssert(p.nodes[root_ob.name_val_pairs[3].value_node_index].value.double_v == 27.0);



		testAssert(root_ob.name_val_pairs[7].name == "spouse");
		testAssert(p.nodes[root_ob.name_val_pairs[7].value_node_index].type == JSONNode::Type_Null);
	}
	catch(Indigo::Exception& e)
	{
		failTest(e.what());
	}


	//--------- Test empty array ----------
	try
	{
		JSONParser p;
		std::string s = "{ \"a\": [] }";
		p.parseBuffer(s.data(), s.size());

		JSONNode root_ob = p.nodes[0];
		testAssert(root_ob.type == JSONNode::Type_Object);
		testAssert(root_ob.name_val_pairs.size() == 1);

		testAssert(root_ob.name_val_pairs[0].name == "a");
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].type == JSONNode::Type_Array);
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].child_indices.size() == 0);
	}
	catch(Indigo::Exception& e)
	{
		failTest(e.what());
	}

	//--------- Test empty object ----------
	try
	{
		JSONParser p;
		std::string s = "{ \"a\": {} }";
		p.parseBuffer(s.data(), s.size());

		JSONNode root_ob = p.nodes[0];
		testAssert(root_ob.type == JSONNode::Type_Object);
		testAssert(root_ob.name_val_pairs.size() == 1);

		testAssert(root_ob.name_val_pairs[0].name == "a");
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].type == JSONNode::Type_Object);
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].name_val_pairs.empty());
	}
	catch(Indigo::Exception& e)
	{
		failTest(e.what());
	}

	//--------- Test empty string ----------
	try
	{
		JSONParser p;
		std::string s = "{ \"a\": \"\" }";
		p.parseBuffer(s.data(), s.size());

		JSONNode root_ob = p.nodes[0];
		testAssert(root_ob.type == JSONNode::Type_Object);
		testAssert(root_ob.name_val_pairs.size() == 1);

		testAssert(root_ob.name_val_pairs[0].name == "a");
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].type == JSONNode::Type_String);
		testAssert(p.nodes[root_ob.name_val_pairs[0].value_node_index].string_v == "");
	}
	catch(Indigo::Exception& e)
	{
		failTest(e.what());
	}

	//--------- Test some numbers ----------
	try
	{
		JSONParser p;
		std::string s = "{ \"a\": [1.0, -1.0, 1, 2, 1e20, -1.0E+20, 15.4e-5] }";
		p.parseBuffer(s.data(), s.size());

		JSONNode root_ob = p.nodes[0];
		testAssert(root_ob.type == JSONNode::Type_Object);
		testAssert(root_ob.name_val_pairs.size() == 1);

		testAssert(root_ob.name_val_pairs[0].name == "a");

		JSONNode& array_node = p.nodes[root_ob.name_val_pairs[0].value_node_index];
		testAssert(array_node.type == JSONNode::Type_Array);
		testAssert(array_node.child_indices.size() == 7);

		testAssert(p.nodes[array_node.child_indices[0]].type == JSONNode::Type_Number);
		testAssert(p.nodes[array_node.child_indices[0]].value.double_v == 1.0);

		testAssert(p.nodes[array_node.child_indices[1]].type == JSONNode::Type_Number);
		testAssert(p.nodes[array_node.child_indices[1]].value.double_v == -1.0);

		testAssert(p.nodes[array_node.child_indices[2]].type == JSONNode::Type_Number);
		testAssert(p.nodes[array_node.child_indices[2]].value.double_v == 1.0);

		testAssert(p.nodes[array_node.child_indices[3]].type == JSONNode::Type_Number);
		testAssert(p.nodes[array_node.child_indices[3]].value.double_v == 2.0);

		testAssert(p.nodes[array_node.child_indices[4]].type == JSONNode::Type_Number);
		testAssert(p.nodes[array_node.child_indices[4]].value.double_v == 1.0e20);

		testAssert(p.nodes[array_node.child_indices[5]].type == JSONNode::Type_Number);
		testAssert(p.nodes[array_node.child_indices[5]].value.double_v == -1.0e20);
		
		testAssert(p.nodes[array_node.child_indices[6]].type == JSONNode::Type_Number);
		testAssert(p.nodes[array_node.child_indices[6]].value.double_v == 15.4e-5);

	}
	catch(Indigo::Exception& e)
	{
		failTest(e.what());
	}

	testStringEscapeSequence("\\\"", "\""); //		\" should decode to "
	testStringEscapeSequence("\\\\", "\\"); //		backslash backslash should decode to backslash. 
	testStringEscapeSequence("\\/", "/");
	testStringEscapeSequence("\\b", "\b");
	testStringEscapeSequence("\\f", "\f");
	testStringEscapeSequence("\\n", "\n");
	testStringEscapeSequence("\\r", "\r");
	testStringEscapeSequence("\\t", "\t");
	
	testStringEscapeSequence("\\u0000", UTF8Utils::encodeCodePoint(0));
	testStringEscapeSequence("\\u1234", UTF8Utils::encodeCodePoint(0x1234));
	testStringEscapeSequence("\\uabCD", UTF8Utils::encodeCodePoint(0xABCD));


	testInvalidStringEscapeSequence("\\");
	testInvalidStringEscapeSequence("\\a");
	testInvalidStringEscapeSequence("\\\0");

	testInvalidStringEscapeSequence("\\u0");
	testInvalidStringEscapeSequence("\\u00");
	testInvalidStringEscapeSequence("\\u000");
	testInvalidStringEscapeSequence("\\uz");
	testInvalidStringEscapeSequence("\\u0z");
	testInvalidStringEscapeSequence("\\u00z");
	testInvalidStringEscapeSequence("\\u000z");
}


#endif // BUILD_TESTS
