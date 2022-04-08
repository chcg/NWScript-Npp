/** @file NWScriptCompiler.cpp
 * Invokes various functions from NscLib compiler/interpreter library.
 *
 **/
 // Copyright (C) 2022 - Leonardo Silva 
 // The License.txt file describes the conditions under which this software may be distributed.


#include "pch.h"
//#include <fstream>
//#include "jpcre2.h"

#include "Utf8_16.h"
#include "NWScriptCompiler.h"
#include "VersionInfoEx.h"

using namespace NWScriptPlugin;
typedef NWScriptLogger::LogType LogType;

#define DEPENDENCYHEADER " \
/*************************************************************************************** \r\n\
 * Dependency files descriptor for \"%DEPENDENCYFILE%\"\r\n\
 * Generated by NWScript Tools for Notepad++ (%VERSION%)\r\n\
 *\r\n\
 * Generation date: %GENERATIONDATE%\r\n\
 ***************************************************************************************/\r\n\
\r\n\
"

#define SCRIPTERRORPREFIX "Error"
#define FORMATDISASMREGEX R"(.+)"
#define DEPENDENCYPARSEREGEX R"(([^\/]+)\/([^\\\n]+))"

typedef jpcre2::select<char> pcre2;
static pcre2::Regex assemblyLine(FORMATDISASMREGEX, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
static pcre2::Regex dependencyParse(DEPENDENCYPARSEREGEX, 0, jpcre2::JIT_COMPILE);

bool NWScriptCompiler::initialize() {

    // Critical path, initialize resources
    try
    {
        _resourceManager = std::make_unique<ResourceManager>(&_logger);
    }
    catch (std::runtime_error& e)
    {
        _logger.log("Failed to initialize the resources manager: " + std::string(e.what()), LogType::Critical, "NSC2001");
        return false;
    }

    NWNHome = getNwnHomePath(_settings->compileVersion);

    return true;
}

bool NWScriptCompiler::loadScriptResources()
{
    ResourceManager::ModuleLoadParams LoadParams;
    ResourceManager::StringVec KeyFiles;

    ZeroMemory(&LoadParams, sizeof(LoadParams));

    LoadParams.SearchOrder =  ResourceManager::ModSearch_PrefDirectory;
    LoadParams.ResManFlags =  ResourceManager::ResManFlagNoGranny2;
    LoadParams.ResManFlags |= ResourceManager::ResManFlagErf16;

    if (_settings->compileVersion == 174) 
    {
#ifdef _WINDOWS
        KeyFiles.push_back("data\\nwn_base");
#else
        KeyFiles.emplace_back("data/nwn_base");
#endif // _WINDOWS
    }
    else 
    {
        KeyFiles.emplace_back("xp3");
        KeyFiles.emplace_back("xp2patch");
        KeyFiles.emplace_back("xp2");
        KeyFiles.emplace_back("xp1");
        KeyFiles.emplace_back("chitin");
    }

    LoadParams.KeyFiles = &KeyFiles;
    LoadParams.ResManFlags |= ResourceManager::ResManFlagBaseResourcesOnly;

    // Legacy code is using ASCII string names. We convert here. Also, many exceptions thrown inside those classes to deal with.
    std::string InstallDir = _settings->getChosenInstallDir() + "\\";
    try {
        _resourceManager->LoadScriptResources(wstr2str(NWNHome), InstallDir, &LoadParams);
    }
    catch(...) { 
        //_resourceManager is writting to the log messages here, so we just return false.
        return false;
    }

    return true;
}

void NWScriptCompiler::processFile(bool fromMemory, char* fileContents)
{
    NWN::ResType fileResType;
    NWN::ResRef32 fileResRef;
    std::string inFileContents;

    // First check: safeguard from trying to recompile nwscript.nss
    if (_stricmp(_sourcePath.filename().string().c_str(), "nwscript.nss") == 0 && _compilerMode == 0)
    {
        _logger.log("Compiling script: " + _sourcePath.string(), LogType::ConsoleMessage);
        _logger.log("Error: you can't explicitly compile any script named \"nwscript.nss\", this name is reserved for the main engine.", LogType::Critical, "NSC2010");
        _logger.log("File ignored: " + _sourcePath.string() , LogType::Info);
        notifyCaller(false);
        return;
    }

    // Initialize the compiler if not already
    if (!isInitialized())
    {
        _logger.log("Initializing compiler...", LogType::ConsoleMessage);
        _logger.log("", LogType::ConsoleMessage);

        if (!initialize())
        {
            notifyCaller(false);
            return;
        }

        // Start building up search paths. 
        _includePaths.push_back(wstr2str(_sourcePath.parent_path()));

        if (!_settings->ignoreInstallPaths)
        {
            if (!loadScriptResources())
            {
                _logger.log("Could not load script resources on installation path: " + _settings->getChosenInstallDir(), LogType::Warning);
            }

            if (_settings->compileVersion == 174)
            {
                std::string overrideDir = _settings->getChosenInstallDir() + "\\ovr\\";
                _includePaths.push_back(overrideDir);
            }
        }

        for (generic_string s : _settings->getIncludeDirsV())
        {
            _includePaths.push_back(properDirNameA(wstr2str(s)) + "\\");
        }

        // Create our compiler/disassembler
        _compiler = std::make_unique<NscCompiler>(*_resourceManager, _settings->useNonBiowareExtenstions);
        _compiler->NscSetIncludePaths(_includePaths);
        _compiler->NscSetCompilerErrorPrefix(SCRIPTERRORPREFIX);
        _compiler->NscSetResourceCacheEnabled(true);
    }

    // Acquire information about NWN Resource Type of the file. Warning of ignored result is incorrect.
#pragma warning (push)
#pragma warning (disable : 6031)
    fileResType = _resourceManager->ExtToResType(wstr2str(_sourcePath).c_str());
    fileResRef = _resourceManager->ResRef32FromStr(wstr2str(_sourcePath.stem()).c_str());
#pragma warning (pop)

    // Load file from disk if not from memory
    if (fromMemory)
        inFileContents = fileContents;
    else
    {
        if (!fileToBuffer(_sourcePath.c_str(), inFileContents))
        {
            _logger.log("Could not load the specified file: " + wstr2str(_sourcePath), LogType::Critical, "NSC2002");
            notifyCaller(false);
            return;
        }
    }

    // Determines file encoding. Only a minimal sample is used here since
    // we are not interested in capturing UTF-8 multibyte-like strings, only UTF-16 types.
    constexpr const int blockSize = IS_TEXT_UNICODE_STATISTICS;
    Utf8_16_Read utfConverter;
    int encoding = utfConverter.determineEncoding((unsigned char*)inFileContents.c_str(), (blockSize > inFileContents.size()) ? inFileContents.size() : blockSize);
    if (encoding == uni16BE || encoding == uni16LE || encoding == uni16BE_NoBOM || encoding == uni16LE_NoBOM)
    {
        std::ignore = utfConverter.convert(inFileContents.data(), inFileContents.size());
        inFileContents.assign(utfConverter.getNewBuf(), utfConverter.getNewSize());
    }

    // Execute the process
    bool bSuccess = false;
    if (_compilerMode == 0)
    {
        if (_fetchPreprocessorOnly)
            _logger.log("Fetching preprocessor output for: " + _sourcePath.string(), LogType::ConsoleMessage);
        else
            _logger.log("Compiling script: " + _sourcePath.string(), LogType::ConsoleMessage);
        bSuccess = compileScript(inFileContents, fileResType, fileResRef);
    }
    else
    {
        _logger.log("Disassembling binary: " + _sourcePath.string(), LogType::ConsoleMessage);
        bSuccess = disassembleBinary(inFileContents, fileResType, fileResRef);
    }

    notifyCaller(bSuccess);
}


bool NWScriptCompiler::compileScript(std::string& fileContents,
    const NWN::ResType& fileResType, const NWN::ResRef32& fileResRef)
{
    // We always ignore include files. And for our project, the compiler ALWAYS
    // return include dependencies, since message filters are done in a higher application layer.
    bool bIgnoreIncludes = true;
    bool bOptimize = _settings->optimizeScript;
    UINT32 compilerFlags = _settings->compilerFlags;
    compilerFlags |= NscCompilerFlag_ShowIncludes;

    // Disable processing overhead for preprocessor messages..
    // Also, since warnings are the type of return, we don't want to suppress them here, no matter what.
    if (_fetchPreprocessorOnly)
    {
        compilerFlags &= ~NscCompilerFlag_GenerateMakeDeps;
        bOptimize = false;
        compilerFlags &= ~NscCompilerFlag_SuppressWarnings;
        compilerFlags |= NscCompilerFlag_ShowPreprocessed;
    }

    // Here we are solely worried about creating a human-readable dependencies view
    if (_makeDependencyView)
    {
        compilerFlags |= NscCompilerFlag_GenerateMakeDeps;
        compilerFlags |= NscCompilerFlag_SuppressWarnings;
        bOptimize = false;
    }

    // HACK: Need to know if this will ever be used on this project (we already have a disassembly option, this one generates PCODE while compiling also).
    //compilerFlags |= NscCompilerFlag_DumpPCode;

    // Main compilation step
    std::string dataRef;                     // Buffer to file is generic and requires a std::string
    swutil::ByteVec generatedCode;
    swutil::ByteVec debugSymbols;
    std::set<std::string> fileDependencies;

    NscResult result = _compiler->NscCompileScript(fileResRef, fileContents.c_str(), fileContents.size(), _settings->compileVersion,
        bOptimize, bIgnoreIncludes, &_logger, compilerFlags, generatedCode, debugSymbols, fileDependencies, _settings->generateSymbols);

    switch (result)
    {
    case NscResult_Failure:
    {
        _logger.log("", LogType::ConsoleMessage);
        _logger.log("Compilation aborted with errors.", LogType::ConsoleMessage);
        _logger.log("", LogType::ConsoleMessage);
        return false;
    }

    case NscResult_Include:
    {
        _logger.log(_sourcePath.filename().string() + " is an include file, ignored.", LogType::ConsoleMessage);
        return true;
    }

    case NscResult_Success:
        break;

    default:
        _logger.log("", LogType::ConsoleMessage);
        _logger.log("Unknown status code", LogType::Critical, "NSC2004");
        _logger.log("", LogType::ConsoleMessage);
        return false;
    }

    // If we are only to fetch preprocessor code, we're done here (since the _logger takes care of that inside the Compile function)
    if (_fetchPreprocessorOnly)
        return true;

    // If we are to create human-readable dependencies, return that
    if (_makeDependencyView)
        return MakeDependenciesView(fileDependencies);

    // Now save code data
    generic_string outputPath = str2wstr(_destDir.string() + "\\" + _sourcePath.stem().string() + compiledScriptSuffix);
    dataRef.assign(reinterpret_cast<char*>(&generatedCode[0]), generatedCode.size());
    if (!bufferToFile(outputPath, dataRef))
    {
        _logger.log("", LogType::ConsoleMessage);
        _logger.log(TEXT("Could not write compiled output file: ") + outputPath, LogType::Critical, TEXT("NSC2005"));
        _logger.log("", LogType::ConsoleMessage);
        return false;
    }

    // Save debug symbols if apply
    if (_settings->generateSymbols)
    {
        dataRef.clear();
        outputPath = str2wstr(_destDir.string() + "\\" + _sourcePath.stem().string() + debugSymbolsFileSuffix);
        dataRef.assign(reinterpret_cast<char*>(&debugSymbols[0]), debugSymbols.size());
        if (!bufferToFile(outputPath, dataRef))
        {
            _logger.log("", LogType::ConsoleMessage);
            _logger.log(TEXT("Could not write generated symbols output file: ") + outputPath, LogType::Critical, TEXT("NSC2006"));
            _logger.log("", LogType::ConsoleMessage);
            return false;
        }
    }

    // And file dependencies if apply
    if (_settings->compilerFlags & NscCompilerFlag_GenerateMakeDeps)
        return MakeDependenciesFile(fileDependencies);

    return true;
}

bool NWScriptCompiler::disassembleBinary(std::string& fileContents,
    const NWN::ResType& fileResType, const NWN::ResRef32& fileResRef)
{
    std::string generatedCode;

    // Main disassemble step.
    _compiler->NscDisassembleScript(fileContents.c_str(), fileContents.size(), generatedCode);

    // This is the way the library returns errors to us on that routine... :D
    if (generatedCode == "DISASSEMBLY ERROR: COMPILER INITIALIZATION FAILED!")
    {
        _logger.log("", LogType::ConsoleMessage);
        _logger.log("Disassembler - Compiler Initialization failed!", LogType::Critical, "NSC2007");
        _logger.log("", LogType::ConsoleMessage);
        return false;
    }

    // Save file, but first, we remove extra carriage returns the library is generating...
    generic_string outputPath = str2wstr(_destDir.string() + "\\" + _sourcePath.stem().string() + disassembledScriptSuffix);
    
    std::stringstream formatedCode;
    pcre2::VecNum matches;
    pcre2::RegexMatch fileMatcher(&assemblyLine);
    size_t lineCount = fileMatcher.setSubject(generatedCode).setModifier("gm").setNumberedSubstringVector(&matches).match();

    for (size_t i = 0; i < lineCount; i++)
        formatedCode << matches[i][0];

    if (!bufferToFile(outputPath, formatedCode.str()))
    {
        _logger.log("", LogType::ConsoleMessage);
        _logger.log(TEXT("Could not write disassembled output file: ") + outputPath, LogType::Critical, TEXT("NSC2008"));
        _logger.log("", LogType::ConsoleMessage);
        return false;
    }

    return true;
}

bool NWScriptCompiler::MakeDependenciesView(const std::set<std::string>& dependencies)
{
    // Generate some timestamp headers
    char timestamp[128]; time_t currTime;  struct tm currTimeP;
    time(&currTime);
    errno_t error = localtime_s(&currTimeP, &currTime);
    strftime(timestamp, 64, "%B %d, %Y - %R", &currTimeP);

    // Get version from module's binary file
    VersionInfoEx versionInfo = VersionInfoEx::getLocalVersion();
    std::stringstream sVersion = {};
    sVersion << "version " << versionInfo.shortString().c_str() << " - build " << versionInfo.build();

    std::map<std::string, std::string> variablesMap;

    variablesMap.insert({ "%DEPENDENCYFILE%", _sourcePath.filename().string() });
    variablesMap.insert({ "%VERSION%", sVersion.str() });
    variablesMap.insert({ "%GENERATIONDATE%", timestamp });

    // Input header information
    std::stringstream sdependencies;
    sdependencies << replaceStringsA(DEPENDENCYHEADER, variablesMap);

    // Main dependencies
    sdependencies << "  1) Main file relation (compiled script -> script)" << "\r\n\r\n";
    sdependencies << "     Source Directory: " + _sourcePath.parent_path().string() << "\r\n";
    sdependencies << "     Destination Directory: " + _destDir.string() << "\r\n";
    sdependencies << "          " + _sourcePath.stem().string() << compiledScriptSuffix << 
        " <- is generated from -> " << _sourcePath.stem().string() << textScriptSuffix << "\r\n\r\n";

    // Additional dependencies
    if (!dependencies.empty())
    {
        sdependencies << "  2) Dependencies of script source: " << _sourcePath.stem().string() << textScriptSuffix << "\r\n\r\n";

        pcre2::VecNum matches;
        pcre2::RegexMatch dependencyParser(&dependencyParse);
        dependencyParser.setNumberedSubstringVector(&matches);

        // Get first path in dependencies for comparisons.
        auto it = dependencies.begin();
        int count = dependencyParser.setSubject(*it).match();
        filesystem::path currentPath = matches[0][1];
        filesystem::path comparePath;

        // For each different path, we write the topic information of that folder and then enumerate the files
        int topicNumber = 1;
        bool bTopicWritten = false;
        for (auto& dependency : dependencies)
        {
            count = dependencyParser.setSubject(dependency).match();
            comparePath = matches[0][1];     // first capture group = directory name.
            if (currentPath != comparePath)
            {
                sdependencies << "\r\n";
                currentPath = comparePath;
                bTopicWritten = false;
                topicNumber++;
            }

            if (!bTopicWritten)
            {
                sdependencies << "        2." << topicNumber << ") Dependencies from: " << currentPath.string() << "\r\n\r\n";
                bTopicWritten = true;
            }

            sdependencies << "                   -> " << matches[0][2] << "\r\n"; // Second capture group = filename
        }

        sdependencies << "\r\n\r\n";
        sdependencies << "------------------[ END OF FILE DEPENDENCIES ]------------------" << "\r\n\r\n";

        _logger.setProcessorString(sdependencies.str());
    }

    return true;
}

bool NWScriptCompiler::MakeDependenciesFile(const std::set<std::string>& dependencies)
{

    // Additional dependencies
    if (!dependencies.empty())
    {
        std::stringstream sdependencies;

        sdependencies << _sourcePath.stem() << compiledScriptSuffix << ": " << _sourcePath.stem() << textScriptSuffix;

        for (auto& dep : dependencies)
            sdependencies << " \\\n    " << dep.c_str();

        for (auto& dep : dependencies)
            sdependencies << "\n" << dep.c_str() << "\n";

        generic_string outputPath = str2wstr(_destDir.string() + "\\" + _sourcePath.stem().string() + dependencyFileSuffix);
        if (!bufferToFile(outputPath, sdependencies.str()))
        {
            _logger.log("", LogType::ConsoleMessage);
            _logger.log(TEXT("Could not write dependency file: ") + outputPath, LogType::Critical, TEXT("NSC2009"));
            _logger.log("", LogType::ConsoleMessage);
            return false;
        }
    }

    return true;
}