/** @file NWScriptParser.cpp
 * Parser for a NWScript.nss file
 *
 **/
 // Copyright 2022 by Leonardo Silva 
 // The License.txt file describes the conditions under which this software may be distributed.

#include <memory>
#include <string>
#include <sstream>
#include <algorithm>
#include <utility>
#include <tchar.h>
#include <sys/stat.h>
#include <fstream>
#include <cctype>
#include <iterator>
#include <locale>


// We switched from std::regex to boost because std:regex was very slow and with poor features...
// then we switched again from boost to PCRE2 because boost doesn't support regex subroutines and
// PCRE2 performance was even greater.
// Projects involved:
// https://github.com/PhilipHazel/pcre2
// And a class wrapper:
// https://github.com/jpcre2/
#ifdef USEBOOSTREGEX
#undef USEBOOSTREGEX
#endif

#ifndef USEBOOSTREGEX
#include "jpcre2.hpp"
#else
// #include <regex>
#include "boost/regex.hpp"
#endif

#include "Utf8_16.h"
#include "NWScriptParser.h"


#ifdef USEADVANCEDENCODINGDETECTION
// This library is good to detect any character encoding. But it's more for an editor than a file parser
#include "uchardet.h"
#endif

#define __L(x) L##x
#define L(x) __L(x)


//:://///////////////////////////////////////////////////////////
// Comments on RegEx by the author...
// I've built all regular expressions used here within this site:
// https://regex101.com/
// 
// I'm not affiliated with them in anyway. In fact, I felt
// like donating there for the fantastic debbuggin features
// and testing they provided. Without their help, this task would
// be exponentialy harder and time consuming.
//:://///////////////////////////////////////////////////////////

// Robust regex definitions are slower, but can handle object nesting, 
// inline comments and validates functions parameters. But since PCRE2
// engine was SO fast in parsing it, the performance degratation was all but
// neglectable, so now we'll use ROBUST definitions always.

#define ROBUSTREGEXDEFINITIONS
#ifdef ROBUSTREGEXDEFINITIONS
const std::string BASEREGEX(R"((?(DEFINE)(?'commentLine'(?>\/\/(?>\n|.*)))(?'comment'(?>\/\*(?>.|\n)*?\*\/))(?'c'(?>\s*+(?>\g<comment>|\g<commentLine>)?\s*+)*+)(?<token>(?>(?!(\/\/|\/\*))("(?:\\.|[^"\\])*"|[\w\d.\-]+)))(?<tokenVector>(?>\[\g<c>(?>(?>\g<token>|\g<tokenVector>|\g<object>)\g<c>,?\g<c>)*?\g<c>\]))(?<object>(?>\{\g<c>(?>(?>\g<token>|\g<tokenVector>|\g<object>)\g<c>,?\g<c>)*?\g<c>\}))(?'param'(?>\g<c>(?>const)?\g<c>(?>(?#paramType)\w+)\g<c>(?>(?#paramName)\w+)\g<c>(?>=\g<c>(?>(?#paramDefaultValue)\g<token>|\g<tokenVector>|\g<object>)){0,1})\g<c>)))");
const std::string ENGINESTRUCTREGEX(R"(^\s*+\K(?>#define)\s++(?>ENGINE_STRUCTURE_\d++)\s++(?<name>\w++))");
const std::string FUNCTIONDECLARATIONREGEX = BASEREGEX + R"(^\g<comment>*+\K(?<type>(?>(?!(return|if|else|switch))\w+))\g<c>(?<name>(?>\w+))\g<c>\((?<parametersString>(?>(?>\g<param>,(?=\g<param>)|\g<param>(?=\))))*+)\)\g<c>;)";
const std::string FUNTIONPARAMETERREGEX = BASEREGEX + R"(\g<c>(?>const){0,1}\g<c>(?<type>(?>\w+))\g<c>(?<name>(?>\w+))\g<c>(?>=\g<c>(?<defaultValue>(?>\g<token>|\g<tokenVector>|\g<object>)))?\g<c>(?!,)?)";
const std::string CONSTANTREGEX = BASEREGEX + R"(^\g<comment>*+(?>(?>const)?\g<c>^\K(?<type>(?>\w+))\g<c>(?<name>(?>\w+))\g<c>=\g<c>(?<value>\g<token>|\g<tokenVector>|\g<object>))\g<c>;)";
#else
const std::string BASEREGEX(R"((?(DEFINE)(?<token>(?>("(?:\\.|[^"\\])*"|[\w\d.\-]+)))(?<tokenVector>(\[(?>\s*+(?>\g<token>|\g{5})\s*+,?)*+\s*+\]))(?<object>(\{(?>\s*+(?>\g<token>|\g<tokenVector>)\s*+,?)*+\s*+\}))))");
const std::string ENGINESTRUCTREGEX(R"(^\s*+\K(?>#define)\s++(?>ENGINE_STRUCTURE_\d++)\s++(?<name>\w++))");
const std::string FUNCTIONDECLARATIONREGEX(R"(^\s*+(?<type>(?!(?>return|if|else))\w++)\s++(?<name>\w+)\s*+\(\s*+(?<parametersString>(?>[\w\s=\-\.\[\]\,\{\}]|(?>"(?>\\.|[^"\\])*+")*+)*+)\)\s*+;)");
const std::string FUNTIONPARAMETERREGEX = BASEREGEX + R"((?>const){0,1}\s*+(?<type>\w++)\s++(?<name>\w++)\s*+(?>=\s*+(?<defaultValue>(?>(?>\g<token>|\g<tokenVector>|\g<object>))))?,?)";
const std::string CONSTANTREGEX = BASEREGEX + R"(^\s*+(?>const)?\s*+(?<type>\w++)\s++(?<name>\w++)\s*+=?\s*+(?<value>\g<token>|\g<tokenVector>|\g<object>)\s*+;)";
#endif

constexpr const int blockSize = 128 * 1024 + 4;

using namespace NWScriptPlugin;

bool NWScriptParser::ParseFile(const generic_string& sFileName, ScriptParseResults& outParseResults)
{
	// First resolve possible file link
	const rsize_t longFileNameBufferSize = MAX_PATH; 
	if (sFileName.size() >= longFileNameBufferSize - 1) 
		return false;

	generic_string targetFileName = sFileName;
	ResolveLinkFile(targetFileName);
	TCHAR longFileName[longFileNameBufferSize];

	const DWORD getFullPathNameResult = ::GetFullPathName(targetFileName.c_str(), longFileNameBufferSize, longFileName, NULL);
	if (getFullPathNameResult == 0)
	{
		return false;
	}
	if (getFullPathNameResult > longFileNameBufferSize)
	{
		return false;
	}
	assert(_tcslen(longFileName) == getFullPathNameResult);

	if (_tcschr(longFileName, '~'))
	{
		// ignore the returned value of function due to win64 redirection system
		::GetLongPathName(longFileName, longFileName, longFileNameBufferSize);
	}

	// Reconvert back filename to stop working with TCHAR pointers
	targetFileName = longFileName;
	
	// Read the raw file contents
	std::string sFileContents;
	bool success = FileToBuffer(targetFileName, sFileContents);

	// Determines file encoding. We are using a maximum fixed block size to that.
	// uni8Bit is also returned by pure ASCII files.
	int encoding = Utf8_16_Read::determineEncoding((unsigned char*)sFileContents.c_str(), (blockSize > sFileContents.size()) ? sFileContents.size() : blockSize);
#ifdef USEADVANCEDENCODINGDETECTION
	// Cannot determine file encoding.. try a more advanced method
	if (encoding == -1)
		encoding = detectCodepage(char*)sFileContents.c_str(), (blockSize > sFileContents.size()) ? sFileContents.size() : blockSize);
#endif

	if (encoding == uni8Bit || encoding == uni7Bit || encoding == uniCookie)
	{
		CreateNWScriptStructureA(sFileContents, outParseResults);
	}
	else if (encoding == uni16BE || encoding == uni16LE || encoding == uni16BE_NoBOM || encoding == uni16LE_NoBOM)
	{
		CreateNWScriptStructureW(sFileContents, outParseResults);
	}

	// Before returning to the caller, we sort the structure to better presentation and so AutoComplete can work properly.
	std::sort(outParseResults.Members.begin(), outParseResults.Members.end(), 
		[](ScriptMember a, ScriptMember b)	{  return a.sName < b.sName;});

	return true;

}

void NWScriptParser::ResolveLinkFile(generic_string& linkFilePath)
{
	IShellLink* psl;
	WCHAR targetFilePath[MAX_PATH];
	WIN32_FIND_DATA wfd = {};

	HRESULT hres = CoInitialize(NULL);
	if (SUCCEEDED(hres))
	{
		hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
		if (SUCCEEDED(hres))
		{
			IPersistFile* ppf;
			hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
			if (SUCCEEDED(hres))
			{
				// Load the shortcut. 
				hres = ppf->Load(linkFilePath.c_str(), STGM_READ);
				if (SUCCEEDED(hres) && hres != S_FALSE)
				{
					// Resolve the link. 
					hres = psl->Resolve(NULL, 0);
					if (SUCCEEDED(hres) && hres != S_FALSE)
					{
						// Get the path to the link target. 
						hres = psl->GetPath(targetFilePath, MAX_PATH, (WIN32_FIND_DATA*)&wfd, SLGP_SHORTPATH);
						if (SUCCEEDED(hres) && hres != S_FALSE)
						{
							linkFilePath = targetFilePath;
						}
					}
				}
				ppf->Release();
			}
			psl->Release();
		}
		CoUninitialize();
	}
}

bool NWScriptParser::FileToBuffer(const generic_string& fileName, std::string& sContents)
{
	std::ifstream fileReadStream;

	fileReadStream.open(fileName.c_str(), std::ios::in | std::ios::binary);
	if (!fileReadStream.is_open())
	{
		return false;
	}

	fileReadStream.seekg(0, std::ios::end);
	sContents.resize(static_cast<std::size_t>(fileReadStream.tellg()));
	fileReadStream.seekg(0, std::ios::beg);
	size_t fileSize = sContents.size();
	fileReadStream.read(&sContents[0], fileSize);
	fileReadStream.close();

	return true;
}

// Switched to PCRE2
#ifndef USEBOOSTREGEX
void NWScriptParser::CreateNWScriptStructureA(const std::string& sFileContents, ScriptParseResults& outParseResults)
{
	typedef jpcre2::select<char> pcre2;

	// We create and compile regexes only once
	static const pcre2::Regex engineStructRegEx(ENGINESTRUCTREGEX, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
	static const pcre2::Regex functionsDeclarationRegEx(FUNCTIONDECLARATIONREGEX, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
	static const pcre2::Regex functionsParamRegEx(FUNTIONPARAMETERREGEX, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
	static const pcre2::Regex constantsRegEx(CONSTANTREGEX, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);

	// Reserve a good amount of space since we're not doing dynamic reallocations. 1 member per line is more than enough
	// Try standard EOL mode (\n). Then alternative (\r). Minimum to reserve is assumed to 1 member since at least 1 line 
	// will be processed by the RegEx.
	size_t lineCount = std::count(sFileContents.begin(), sFileContents.end(), '\n');
	if (lineCount == 0)
		lineCount = std::count(sFileContents.begin(), sFileContents.end(), '\r');

	outParseResults.Members.reserve(1 + lineCount);

	// Create our Match object and a named capture substring vector
	pcre2::VecNas captureGroup;
	pcre2::RegexMatch regexMatch(&engineStructRegEx);
	regexMatch.setSubject(sFileContents);
	regexMatch.addModifier("gm");
	regexMatch.setNamedSubstringVector(&captureGroup);
	size_t count = regexMatch.match();

	// Iterate through results. Since we're expecting generic_strings here
	// we also do character conversion.
	outParseResults.EngineStructuresCount = count;
	for (size_t i = 0; i < count;i++)
	{
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::EngineStruct; n.sName = str2wstr(captureGroup[i]["name"]);
		outParseResults.Members.emplace_back(n);
	}

	regexMatch.setRegexObject(&functionsDeclarationRegEx);
	count = regexMatch.match();

	outParseResults.FunctionsCount = count;

	pcre2::RegexMatch regexMatchSubString(&functionsParamRegEx);
	regexMatchSubString.addModifier("gm");
	pcre2::VecNas captureSubGroup;
	regexMatchSubString.setNamedSubstringVector(&captureSubGroup);
	for (size_t i = 0; i < count;i++)
	{
		std::vector<ScriptParamMember> sParams;

		regexMatchSubString.setSubject(captureGroup[i]["parametersString"]);
		size_t params = regexMatchSubString.match();
		for (size_t j = 0; j < params; j++)
		{
			ScriptParamMember s;
			s.sType = str2wstr(captureSubGroup[j]["type"]); s.sName = str2wstr(captureSubGroup[j]["name"]);
			s.sDefaultValue = str2wstr(captureSubGroup[j]["defaultValue"]);
			sParams.push_back(s);
		}

		NWScriptParser::ScriptMember Me;
		Me.mID = MemberID::Function; Me.sType = str2wstr(captureGroup[i]["type"]);
		Me.sName = str2wstr(captureGroup[i]["name"]); Me.sParams = sParams;
		outParseResults.Members.emplace_back(Me);
	}

	regexMatch.setRegexObject(&constantsRegEx);
	count = regexMatch.match();

	outParseResults.ConstantsCount = count;
	for (size_t i = 0; i < count;i++)
	{
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::Constant; n.sType = str2wstr(captureGroup[i]["type"]); n.sName = str2wstr(captureGroup[i]["name"]);
		n.sValue = str2wstr(captureGroup[i]["value"]);;
		outParseResults.Members.emplace_back(n);
	}

	outParseResults.Members.shrink_to_fit();
}

void NWScriptParser::CreateNWScriptStructureW(const std::string& sFileContents, ScriptParseResults& outParseResults)
{
	typedef jpcre2::select<wchar_t> pcre2;

	// We create and compile regexes only once
	static const pcre2::Regex engineStructRegEx(str2wstr(ENGINESTRUCTREGEX), PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
	static const pcre2::Regex functionsDeclarationRegEx(str2wstr(FUNCTIONDECLARATIONREGEX), PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
	static const pcre2::Regex functionsParamRegEx(str2wstr(FUNTIONPARAMETERREGEX), PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
	static const pcre2::Regex constantsRegEx(str2wstr(CONSTANTREGEX), PCRE2_MULTILINE, jpcre2::JIT_COMPILE);

	// Since we're not re-encoding the file, we just assign pointers. Hopefully our encoding detector was right...
	std::wstring sNewFile;
	sNewFile.assign((wchar_t*)sFileContents.c_str(), sFileContents.size() / 2);

	// Reserve a good amount of space since we're not doing dynamic allocations. 1 member per line is more than enough
	// Try standard EOL mode (\n). Then alternative (\r). Minimum to reserve is assumed to 1 member since at least 1 line 
	// will be processed by the RegEx.
	size_t lineCount = std::count(sNewFile.begin(), sNewFile.end(), TEXT('\n'));
	if (lineCount == 0)
		lineCount = std::count(sNewFile.begin(), sNewFile.end(), TEXT('\r'));

	outParseResults.Members.reserve(1 + lineCount);

	// Create our Match object and a named capture substring vector
	pcre2::VecNas captureGroup;
	pcre2::RegexMatch regexMatch(&engineStructRegEx);
	regexMatch.setSubject(sNewFile);
	regexMatch.addModifier("gm");
	regexMatch.setNamedSubstringVector(&captureGroup);
	size_t count = regexMatch.match();

	// Iterate through results. Since we're expecting generic_strings here
	// we also do character conversion.
	outParseResults.EngineStructuresCount = count;
	for (size_t i = 0; i < count;i++)
	{
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::EngineStruct; n.sName = captureGroup[i][L"name"];
		outParseResults.Members.emplace_back(n);
	}

	regexMatch.setRegexObject(&functionsDeclarationRegEx);
	count = regexMatch.match();

	outParseResults.FunctionsCount = count;

	pcre2::RegexMatch regexMatchSubString(&functionsParamRegEx);
	regexMatchSubString.addModifier("gm");
	pcre2::VecNas captureSubGroup;
	regexMatchSubString.setNamedSubstringVector(&captureSubGroup);
	for (size_t i = 0; i < count;i++)
	{
		std::vector<ScriptParamMember> sParams;

		regexMatchSubString.setSubject(captureGroup[i][L"parametersString"]);
		size_t params = regexMatchSubString.match();
		for (size_t j = 0; j < params; j++)
		{
			ScriptParamMember s;
			s.sType = captureSubGroup[j][L"type"]; s.sName = captureSubGroup[j][L"name"];
			s.sDefaultValue = captureSubGroup[j][L"defaultValue"];
			sParams.push_back(s);
		}

		NWScriptParser::ScriptMember Me;
		Me.mID = MemberID::Function; Me.sType = captureGroup[i][L"type"];
		Me.sName = captureGroup[i][L"name"]; Me.sParams = sParams;
		outParseResults.Members.emplace_back(Me);
	}

	regexMatch.setRegexObject(&constantsRegEx);
	count = regexMatch.match();

	outParseResults.ConstantsCount = count;
	for (size_t i = 0; i < count;i++)
	{
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::Constant; n.sType = captureGroup[i][L"type"]; n.sName = captureGroup[i][L"name"];
		n.sValue = captureGroup[i][L"value"];;
		outParseResults.Members.emplace_back(n);
	}

	outParseResults.Members.shrink_to_fit();
}

#else

// Deprecated, because BOOST REGEX doesn't support subroutines.
// We're replacing with PCRE2
void NWScriptParser::CreateNWScriptStructureA(const std::string& sFileContents, ScriptParseResults& outParseResults)
{
	using aregex = boost::basic_regex<char>;
	using aregexmatch = boost::match_results<std::string::const_iterator>;
	using asregex_iterator = boost::regex_iterator<std::string::const_iterator>;
	using asregex_token_iterator = boost::regex_token_iterator<std::string::const_iterator>;
	using amatch = boost::match_results<std::string>;

	// We create and compile regexes only once
	static const aregex sEngineStructRegExA(ENGINESTRUCTREGEX, boost::regex_constants::optimize);
	static const aregex sFunctionsDeclarationRegExA(FUNCTIONDECLARATIONREGEX, boost::regex_constants::optimize);
	static const aregex sFunctionsParamRegExA(FUNTIONPARAMETERREGEX, boost::regex_constants::optimize);
	static const aregex sConstantsRegExA(CONSTANTREGEX, boost::regex_constants::optimize);

	// Reserve a good amount of space since we're not doing dynamic allocations. 1 member per line is more than enough
	// Try standard EOL mode (\n). Then alternative (\r). Minimum to reserve is assumed to 1 member since at least 1 line 
	// will be processed by the RegEx.
	size_t lineCount = std::count(sFileContents.begin(), sFileContents.end(), '\n');
	if (lineCount == 0)
		lineCount = std::count(sFileContents.begin(), sFileContents.end(), '\r');

	outParseResults.Members.reserve(1 + lineCount);

	// Match subgroups for Engine Structures are: <preprocessor> [1], <EngineWord> [2], <name> [3]
	auto sBegin = asregex_iterator(sFileContents.begin(), sFileContents.end(), sEngineStructRegExA);
	auto sEnd = asregex_iterator();
	aregexmatch m, n;

	int iCount = 0;
	for (asregex_iterator i = sBegin; i != sEnd; i++)
	{
		m = *i;
		// We are emplacing back MemberID = EngineStruct, sType = null, sName = <name>, sValue = nullptr, sParams = {empty}
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::EngineStruct; n.sName = str2wstr(m["name"].str().c_str());
		outParseResults.Members.emplace_back(n);
		iCount++;
	}
	outParseResults.EngineStructuresCount = iCount;

	// Match subgroups for Functions are: <type> [1], <name> [2], <params> [3]
	// <params> expands in <ptype> [1], <pname> [2], <pdefaultvalue> [4]
	sBegin = asregex_iterator(sFileContents.begin(), sFileContents.end(), sFunctionsDeclarationRegExA);
	sEnd = asregex_iterator();
	iCount = 0;
	for (asregex_iterator i = sBegin; i != sEnd; i++)
	{
		m = *i;
		std::vector<ScriptParamMember> sParams;

		std::string eMatch = m["parametersString"].str().c_str();
		auto sBegin2 = asregex_iterator(eMatch.begin(), eMatch.end(), sFunctionsParamRegExA);
		auto sEnd2 = asregex_iterator();
		for (asregex_iterator j = sBegin2; j != sEnd2; j++)
		{
			n = *j;
			// Param Default value can be null
			ScriptParamMember s;
			s.sType = str2wstr(n["type"].str().c_str()); s.sName = str2wstr(n["name"].str().c_str());
			s.sDefaultValue = n.size() > 4 ? str2wstr(n["defaultValue"].str().c_str()) : nullptr;
			sParams.push_back(s);
		}

		// We are pushing back MemberID = Function, sType = <type>, sName = <name>, sValue = nullptr, sParams = { <subquery> }
		// {<subquery>} = sType = <ptype>, sName = <pname>, sDefaultValue = <defaultvalue>
		NWScriptParser::ScriptMember Me;
		Me.mID = MemberID::Function; Me.sType = str2wstr(m["type"].str().c_str());
		Me.sName = str2wstr(m["name"].str().c_str()); Me.sParams = sParams;
		outParseResults.Members.emplace_back(Me);
		iCount++;
	}
	outParseResults.FunctionsCount = iCount;

	// Match subgroups for constants are:  <type> [1], <name> [2], <value> [3]
	sBegin = asregex_iterator(sFileContents.begin(), sFileContents.end(), sConstantsRegExA);
	sEnd = asregex_iterator();
	iCount = 0;
	for (asregex_iterator i = sBegin; i != sEnd; i++)
	{
		m = *i;

		// We are emplacing back MemberID = EngineStruct, sType = null, sName = <name>, sValue = nullptr, sParams = {empty}
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::Constant; n.sType = str2wstr(m["type"].str().c_str()), n.sName = str2wstr(m["name"].str().c_str());
		n.sValue = str2wstr(m["value"].str().c_str());
		outParseResults.Members.emplace_back(n);
		iCount++;
	}
	outParseResults.ConstantsCount = iCount;

	// Compacts memory back
	outParseResults.Members.shrink_to_fit();

	return;
}

void NWScriptParser::CreateNWScriptStructureW(const std::string& sFileContents, ScriptParseResults& outParseResults)
{
	using wregex = boost::basic_regex<wchar_t>;
	using wregexmatch = boost::match_results<std::wstring::const_iterator>;
	using wsregex_iterator = boost::regex_iterator<std::wstring::const_iterator>;
	using wsregex_token_iterator = boost::regex_token_iterator<std::wstring::const_iterator>;
	using wmatch = boost::match_results<std::wstring>;

	static const wregex sEngineStructRegExW(str2wstr(ENGINESTRUCTREGEX), boost::regex_constants::optimize);
	static const wregex sFunctionsDefinitionRegExW(str2wstr(FUNCTIONDECLARATIONREGEX), boost::regex_constants::optimize);
	static const wregex sFunctionsParamRegExW(str2wstr(FUNTIONPARAMETERREGEX), boost::regex_constants::optimize);
	static const wregex sConstantsRegExW(str2wstr(CONSTANTREGEX), boost::regex_constants::optimize);

	// Since we're not re-encoding the file, we just assign pointers. Hopefully our encoding detector was right...
	std::wstring sNewFile;
	sNewFile.assign((wchar_t*)sFileContents.c_str(), sFileContents.size() / 2);

	// Reserve a good amount of space since we're not doing dynamic allocations. 1 member per line is more than enough
	// Try standard EOL mode (\n). Then alternative (\r). Minimum to reserve is assumed to 1 member since at least 1 line 
	// will be processed by the RegEx.
	size_t lineCount = std::count(sNewFile.begin(), sNewFile.end(), TEXT('\n'));
	if (lineCount == 0)
		lineCount = std::count(sNewFile.begin(), sNewFile.end(), TEXT('\r'));

	outParseResults.Members.reserve(1 + lineCount);

	// Match subgroups for Engine Structures are: <preprocessor> [1], <EngineWord> [2], <name> [3]
	auto sBegin = wsregex_iterator(sNewFile.begin(), sNewFile.end(), sEngineStructRegExW);
	auto sEnd = wsregex_iterator();
	wregexmatch m, n;

	int iCount = 0;
	for (wsregex_iterator i = sBegin; i != sEnd; i++)
	{
		m = *i;
		// We are emplacing back MemberID = EngineStruct, sType = null, sName = <name>, sValue = nullptr, sParams = {empty}
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::EngineStruct; n.sName = m["name"].str().c_str();
		outParseResults.Members.emplace_back(n);
		iCount++;
	}
	outParseResults.EngineStructuresCount = iCount;

	// Match subgroups for Functions are: <type> [1], <name> [2], <params> [3]
	// <params> expands in <ptype> [1], <pname> [2], <pdefaultvalue> [4]
	sBegin = wsregex_iterator(sNewFile.begin(), sNewFile.end(), sFunctionsDefinitionRegExW);
	sEnd = wsregex_iterator();
	iCount = 0;
	for (wsregex_iterator i = sBegin; i != sEnd; i++)
	{
		m = *i;
		std::vector<ScriptParamMember> sParams;

		std::wstring eMatch = m["parametersString"].str().c_str();
		auto sBegin2 = wsregex_iterator(eMatch.begin(), eMatch.end(), sFunctionsParamRegExW);
		auto sEnd2 = wsregex_iterator();
		for (wsregex_iterator j = sBegin2; j != sEnd2; j++)
		{
			n = *j;
			// Param Default value can be null
			ScriptParamMember s;
			s.sType = n["type"].str().c_str(); s.sName = n["name"].str().c_str();
			s.sDefaultValue = n.size() > 4 ? n["defaultValue"].str().c_str() : nullptr;
			sParams.push_back(s);
		}

		// We are pushing back MemberID = Function, sType = <type>, sName = <name>, sValue = nullptr, sParams = { <subquery> }
		// {<subquery>} = sType = <ptype>, sName = <pname>, sDefaultValue = <defaultvalue>
		NWScriptParser::ScriptMember Me;
		Me.mID = MemberID::Function; Me.sType = m["type"].str().c_str();
		Me.sName = m["name"].str().c_str(); Me.sParams = sParams;
		outParseResults.Members.emplace_back(Me);
		iCount++;
	}
	outParseResults.FunctionsCount = iCount;

	// Match subgroups for constants are:  <type> [1], <name> [2], <value> [3]
	sBegin = wsregex_iterator(sNewFile.begin(), sNewFile.end(), sConstantsRegExW);
	sEnd = wsregex_iterator();
	iCount = 0;
	for (wsregex_iterator i = sBegin; i != sEnd; i++)
	{
		m = *i;

		// We are emplacing back MemberID = EngineStruct, sType = null, sName = <name>, sValue = nullptr, sParams = {empty}
		NWScriptParser::ScriptMember n;
		n.mID = MemberID::Constant; n.sType = m["type"].str().c_str(), n.sName = m["name"].str().c_str();
		n.sValue = m["value"].str().c_str();
		outParseResults.Members.emplace_back(n);
		iCount++;
	}
	outParseResults.ConstantsCount = iCount;

	// Compacts memory back
	outParseResults.Members.shrink_to_fit();

	return;
}


#endif

#ifdef USEADVANCEDENCODINGDETECTION

/*
Requires uchardet library.
https://www.freedesktop.org/wiki/Software/uchardet/
This would be overkill for a mere import... so left unused. :)
*/

int NWScriptParser::detectCodepage(char* buf, size_t len)
{
	int codepage = -1;
	uchardet_t ud = uchardet_new();
	uchardet_handle_data(ud, buf, len);
	uchardet_data_end(ud);
	const char* cs = uchardet_get_charset(ud);
	if (stricmp(cs, "TIS-620") != 0) // TIS-620 detection is disabled here because uchardet detects usually wrongly UTF-8 as TIS-620
		codepage = EncodingMapper::getInstance().getEncodingFromString(cs);
	uchardet_delete(ud);
	return codepage;
}

#endif
