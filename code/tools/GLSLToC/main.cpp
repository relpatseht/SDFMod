#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <SPIRV/GlslangToSpv.h>

typedef std::unordered_map<std::string, uint64_t> FileModifiedMap;
typedef std::vector<std::string> StringList;
typedef std::unordered_map<std::string, std::string> FileCache;

struct SourceFile
{
	std::string fileName;
	StringList includeFiles;
	std::string contents;
	EShLanguage stage;
};

struct Settings
{
	StringList includeDirs;
	std::string forceInclude;
	StringList inputFiles;
	std::string outputDir;
	std::string decoration;
};

static const char* GetFilename(const char* dir)
{
	const char* nextSlash = dir;
	const char* filename;

	do
	{
		filename = nextSlash;
		nextSlash = std::strpbrk(filename + 1, "/\\");
	} while (nextSlash != nullptr);

	return filename == dir ? dir : filename + 1;
}

static bool DirectoryExists(const char *dir)
{
	const DWORD attrib = GetFileAttributes(dir);

	return (attrib != INVALID_FILE_ATTRIBUTES) && (attrib & FILE_ATTRIBUTE_DIRECTORY);
}

static bool FileExists(const char *file)
{
	std::ifstream f(file);

	return f.is_open();
}

static bool IsDirectory(const char *path)
{
	DWORD ftyp = GetFileAttributesA(path);
	if (ftyp == INVALID_FILE_ATTRIBUTES)
		return false;  //something is wrong with your path!

	if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
		return true;   // this is a directory!

	return false;    // this is not a directory!
}

static bool ValidateExtension(const char *path)
{
	const char * const ext = strrchr(path, '.');

	if (ext == nullptr)
	{
		return false;
	}

	switch (tolower(ext[1]))
	{
		case 'v':
			return tolower(ext[2]) != 'c'; // ignore .vcxproj*
		case 'f':
			return tolower(ext[2]) != 'i'; // ignore .filters
		case 'c':
			return true;
		case 'g':
			return tolower(ext[2]) != 'l'; // ignore .glsl
		case 't':
			return (_stricmp(ext + 1, "tesc") == 0) || (_stricmp(ext + 1, "tese") == 0);
	}

	return false;
}

static bool ScanDirForFiles(const char *path, StringList *outFiles)
{
	HANDLE hFind;
	WIN32_FIND_DATA ffd;
	std::string pathStr;
	bool ret;

	pathStr = path;
	pathStr += '\\';

	hFind = FindFirstFile((pathStr + "*.*").c_str(), &ffd);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	ret = true;
	do
	{
		if (ffd.cFileName[0] != '.')
		{
			const std::string file = pathStr + ffd.cFileName;

			if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (!ScanDirForFiles(file.c_str(), outFiles))
				{
					ret = false;
					break;
				}
			}
			else
			{
				if (ValidateExtension(ffd.cFileName))
				{
					outFiles->push_back(std::move(file));
				}
			}
		}
	} while (FindNextFile(hFind, &ffd) != 0);

	if (GetLastError() != ERROR_NO_MORE_FILES)
	{
		ret = false;
	}

	FindClose(hFind);

	return ret;
}

static bool FindFile(const StringList &paths, std::string *inoutPath)
{
	char fileBuf[MAX_PATH];

	if (FileExists(inoutPath->c_str()))
	{
		if (!GetFullPathName(inoutPath->c_str(), sizeof(fileBuf), fileBuf, nullptr))
		{
			return false;
		}

		*inoutPath = fileBuf;
		return true;
	}

	for (size_t pathIndex = 0; pathIndex < paths.size(); ++pathIndex)
	{
		std::string testPath;

		testPath = paths[pathIndex];
		testPath += '\\';
		testPath += *inoutPath;

		if (FileExists(testPath.c_str()))
		{
			if (!GetFullPathName(testPath.c_str(), sizeof(fileBuf), fileBuf, nullptr))
			{
				continue;
			}

			*inoutPath = fileBuf;
			return true;
		}
	}

	return false;
}

static bool ParseSettings(int argc, char *argv[], Settings *outSettings)
{
	for (int argIndex = 1; argIndex < argc; ++argIndex)
	{
		const char * const arg = argv[argIndex];

		if (*arg == '-')
		{
			char fileBuf[MAX_PATH];

			switch (arg[1])
			{
				case 'I':
					if (!GetFullPathName(arg + 2, sizeof(fileBuf), fileBuf, nullptr))
					{
						std::cout << "Couldn't get the full path of \"" << arg + 2 << "\"." << std::endl;
						return false;
					}

					if (!DirectoryExists(fileBuf))
					{
						std::cout << "\"" << fileBuf << "\" is not a valid directory." << std::endl;
						return false;
					}

					outSettings->includeDirs.push_back(fileBuf);
				break;
				case 'D':
					outSettings->decoration = arg + 2;
				break;
				case 'F':
					if (!outSettings->forceInclude.empty())
					{
						std::cout << "Multiple force include files defined." << std::endl;
						return false;
					}

					outSettings->forceInclude = arg + 2;
				break;
				case 'O':
					if (!GetFullPathName(arg + 2, sizeof(fileBuf), fileBuf, nullptr))
					{
						std::cout << "Couldn't get the full path of \"" << arg + 2 << "\"." << std::endl;
						return false;
					}

					if (!DirectoryExists(fileBuf))
					{
						std::cout << fileBuf << " does not exist. Creating." << std::endl;
						if (!std::filesystem::create_directory(fileBuf))
						{
							std::cout << "Failed to create directory \"" << fileBuf << "\"." << std::endl;
							return false;
						}
					}

					if (!outSettings->outputDir.empty())
					{
						std::cout << "Multiple output directories defined." << std::endl;
						return false;
					}

					outSettings->outputDir = fileBuf;
				break;
				default:
					std::cout << "Unkown argument \"" << arg << "\"." << std::endl;
					return false;
			}
		}
		else
		{
			char fileBuf[MAX_PATH];

			if (!GetFullPathName(arg, sizeof(fileBuf), fileBuf, nullptr))
			{
				std::cout << "Couldn't get the full path of \"" << arg << "\"." << std::endl;
				return false;
			}

			if (IsDirectory(fileBuf))
			{
				if (!ScanDirForFiles(fileBuf, &outSettings->inputFiles))
				{
					std::cout << "Failed to scan \"" << fileBuf << "\" for files." << std::endl;
					return false;
				}
			}
			else
			{
				if (!FileExists(fileBuf))
				{
					std::cout << "\"" << fileBuf << "\" is not a valid file." << std::endl;
					return false;
				}

				if (!ValidateExtension(fileBuf))
				{
					std::cout << "\"" << fileBuf << "\" does not have a compileable extension." << std::endl;
					return false;
				}

				outSettings->inputFiles.push_back(fileBuf);
			}
		}
	}

	if (!outSettings->forceInclude.empty())
	{
		if (!FindFile(outSettings->includeDirs, &outSettings->forceInclude))
		{
			std::cout << "\"" << outSettings->forceInclude << "\" is not a valid file." << std::endl;
			return false;
		}

	}

	return true;
}

static void PrintHelp()
{
	std::cout << "GLSLToC: " << std::endl;
	std::cout << "    Converts GLSL files into a C headers suitable for Vulkan consumption." << std::endl;
	std::cout << std::endl;
	std::cout << "Usage: " << std::endl;
	std::cout << "    glsltoc [options] <file1> <file2> ... <fileN>" << std::endl;
	std::cout << std::endl;
	std::cout << "Description:" << std::endl;
	std::cout << "    Will compile one or more GLSL files into SPIRV and output them to" << std::endl;
	std::cout << "    binary files suitable for Vulkan consumption. The type of the shader" << std::endl;
	std::cout << "    is inferred from the extension.  Supported extensions are as follows:" << std::endl;
	std::cout << "        .f[^i]* : Fragment Shader" << std::endl;
	std::cout << "        .v[^c]* : Vertex Shader" << std::endl;
	std::cout << "        .g[^l]* : Geometry Shader" << std::endl;
	std::cout << "        .tesc   : Tesselation Control Shader" << std::endl;
	std::cout << "        .tese   : Tesselation Evaluation Shader" << std::endl;
	std::cout << "        .c*     : Compute Shader" << std::endl;
	std::cout << "    Output files will be given the same name as their inputs with \".h\"" << std::endl;
	std::cout << "    appended. If an output directory is supplied, all source files will" << std::endl;
	std::cout << "    be placed there. If a directory is supplied as an input file, it is" << std::endl;
	std::cout << "    scanned for supported file types. A file will only be compiled if it" << std::endl;
	std::cout << "    has been updated relative to output (as determined by last modified" << std::endl;
	std::cout << "    time)." << std::endl;
	std::cout << "    Include directives are supported, with the ability to specify N" << std::endl;
	std::cout << "    search directories. Additionaly, you may supply a force include file." << std::endl;
	std::cout << "    If supplied, the force include file will be loaded first in the shader," << std::endl;
	std::cout << "    making it ideal for version and extension definitions." << std::endl;
	std::cout << "    Options should be specified without a space between the option" << std::endl;
	std::cout << "    signifier and its text." << std::endl;
	std::cout << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "    -I: An include directory search path. Multiple -I paramaters are accepted." << std::endl;
	std::cout << "    -O: The output directory you'd like the header files placed in." << std::endl;
	std::cout << "    -F: The path to the force include file. Include directories are searched." << std::endl;
	std::cout << std::endl;
}

static uint64_t GetLastModifiedTime(const std::string &file, FileModifiedMap *inoutMap)
{
	const FileModifiedMap::const_iterator fileIt = inoutMap->find(file);

	if (fileIt != inoutMap->cend())
	{
		return fileIt->second;
	}
	else
	{
		const HANDLE hFile = CreateFile(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		uint64_t time;

		time = 0;
		if (hFile != INVALID_HANDLE_VALUE)
		{
			FILETIME lastWrite;

			if (GetFileTime(hFile, nullptr, nullptr, &lastWrite))
			{
				time = (static_cast<uint64_t>(lastWrite.dwHighDateTime) << 32ULL) | lastWrite.dwLowDateTime;

				inoutMap->insert(std::make_pair(file, time));
			}

			CloseHandle(hFile);
		}

		return time;
	}
}

static void UpdateLastModifiedTime(const std::string *files, unsigned count, FileModifiedMap *outMap)
{
	for (unsigned fileIndex = 0; fileIndex < count; ++fileIndex)
	{
		const std::string &file = files[fileIndex];

		GetLastModifiedTime(file, outMap);
	}
}

static bool LoadHeaderFile(const std::string &fileName, const std::string &fileCacheName, const StringList &includeDirs, FileCache *inoutFileCache, StringList* outIncludeFiles);

static bool ParseIncludeDirective(const std::string &line, const StringList &includeDirs, FileCache *inoutFileCache, StringList* outIncludeFiles)
{
	size_t includeStart;
	size_t includeEnd;
	std::string includeFile;
	std::string includeName;

	includeStart = line.find('"');
	if (includeStart == std::string::npos)
	{
		includeStart = line.find('<');
		includeEnd = line.find('>', includeStart + 1);
	}
	else
	{
		includeEnd = line.find('"', includeStart + 1);
	}

	if (includeStart == std::string::npos || includeEnd == std::string::npos)
	{
		std::cout << "Include directive \"" << line << "\" could not be parsed." << std::endl;
		return false;
	}

	includeName = line.substr(includeStart + 1, includeEnd - includeStart - 1);
	includeFile = includeName;
	if (!FindFile(includeDirs, &includeFile))
	{
		std::cout << "Include file \"" << includeFile << "\" not found." << std::endl;
		return false;
	}

	outIncludeFiles->push_back(includeFile);

	if(!LoadHeaderFile(includeFile, includeName, includeDirs, inoutFileCache, outIncludeFiles))
	{
		std::cout << "Failed to load header file \"" << includeFile << "\"." << std::endl;
		return false;
	}

	return true;
}

static bool LoadFileFromStream(std::istream *stream, const StringList &includeDirs, FileCache *inoutFileCache, StringList *outIncludeFiles, std::ostream *outFile)
{
	std::string line;

	while (std::getline(*stream, line))
	{
		if (line.find("#include ") == 0)
		{
			if (!ParseIncludeDirective(line, includeDirs, inoutFileCache, outIncludeFiles))
			{
				return false;
			}
		}

		(*outFile) << line << std::endl;
	}

	return true;
}

static bool LoadHeaderFile(const std::string &fileName, const std::string &fileCacheName, const StringList &includeDirs, FileCache* inoutFileCache, StringList *outIncludeFiles)
{
	if (inoutFileCache->count(fileName) == 0)
	{
		std::ifstream file(fileName.c_str());
		std::ostringstream contents;

		if (!LoadFileFromStream(&file, includeDirs, inoutFileCache, outIncludeFiles, &contents))
		{
			return false;
		}

		inoutFileCache->insert(std::make_pair(fileCacheName, std::move(contents.str())));
	}

	return true;
}

static bool LoadSourceFile(const std::string &fileName, const StringList &includeDirs, FileCache *inoutFileCache, StringList *outIncludeFiles, std::string *outFile)
{
	std::ifstream file(fileName.c_str());
	std::ostringstream contents;

	if (!LoadFileFromStream(&file, includeDirs, inoutFileCache, outIncludeFiles, &contents))
	{
		return false;
	}

	outFile->swap(contents.str());
	
	return true;
}

static bool DetermineStageFromFileName(const std::string &fileName, EShLanguage *outStage)
{
	size_t extStart;

	extStart = fileName.rfind('.');
	if (extStart == std::string::npos)
	{
		std::cout << "No extension on \"" << fileName.c_str() << "\". Excluding from build." << std::endl;
		return false;
	}

	switch (fileName[extStart + 1])
	{
		case 'f': case 'F':
			*outStage = EShLangFragment;
		break;
		case 'v': case 'V':
			*outStage = EShLangVertex;
		break;
		case 'g': case 'G':
			*outStage = EShLangGeometry;
		break;
		case 'c': case 'C':
			*outStage = EShLangCompute;
		break;
		case 't': case 'T':
		{
			std::string ext;
		
			ext = fileName.substr(extStart + 1);
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

			if (ext == "tese")
			{
				*outStage = EShLangTessEvaluation;
				return true;
			}
			else if (ext == "tesc")
			{
				*outStage = EShLangTessControl;
				return true;
			}
		}
		default:
			std::cout << "Unkown extension on \"" << fileName.c_str() << "\". Excluding from build." << std::endl;
			return false;
	}

	return true;
}

static void InitResources(TBuiltInResource *outResources) 
{
	outResources->maxLights = 32;
	outResources->maxClipPlanes = 6;
	outResources->maxTextureUnits = 32;
	outResources->maxTextureCoords = 32;
	outResources->maxVertexAttribs = 64;
	outResources->maxVertexUniformComponents = 4096;
	outResources->maxVaryingFloats = 64;
	outResources->maxVertexTextureImageUnits = 32;
	outResources->maxCombinedTextureImageUnits = 80;
	outResources->maxTextureImageUnits = 32;
	outResources->maxFragmentUniformComponents = 4096;
	outResources->maxDrawBuffers = 32;
	outResources->maxVertexUniformVectors = 128;
	outResources->maxVaryingVectors = 8;
	outResources->maxFragmentUniformVectors = 16;
	outResources->maxVertexOutputVectors = 16;
	outResources->maxFragmentInputVectors = 15;
	outResources->minProgramTexelOffset = -8;
	outResources->maxProgramTexelOffset = 7;
	outResources->maxClipDistances = 8;
	outResources->maxComputeWorkGroupCountX = 65535;
	outResources->maxComputeWorkGroupCountY = 65535;
	outResources->maxComputeWorkGroupCountZ = 65535;
	outResources->maxComputeWorkGroupSizeX = 1024;
	outResources->maxComputeWorkGroupSizeY = 1024;
	outResources->maxComputeWorkGroupSizeZ = 64;
	outResources->maxComputeUniformComponents = 1024;
	outResources->maxComputeTextureImageUnits = 16;
	outResources->maxComputeImageUniforms = 8;
	outResources->maxComputeAtomicCounters = 8;
	outResources->maxComputeAtomicCounterBuffers = 1;
	outResources->maxVaryingComponents = 60;
	outResources->maxVertexOutputComponents = 64;
	outResources->maxGeometryInputComponents = 64;
	outResources->maxGeometryOutputComponents = 128;
	outResources->maxFragmentInputComponents = 128;
	outResources->maxImageUnits = 8;
	outResources->maxCombinedImageUnitsAndFragmentOutputs = 8;
	outResources->maxCombinedShaderOutputResources = 8;
	outResources->maxImageSamples = 0;
	outResources->maxVertexImageUniforms = 0;
	outResources->maxTessControlImageUniforms = 0;
	outResources->maxTessEvaluationImageUniforms = 0;
	outResources->maxGeometryImageUniforms = 0;
	outResources->maxFragmentImageUniforms = 8;
	outResources->maxCombinedImageUniforms = 8;
	outResources->maxGeometryTextureImageUnits = 16;
	outResources->maxGeometryOutputVertices = 256;
	outResources->maxGeometryTotalOutputComponents = 1024;
	outResources->maxGeometryUniformComponents = 1024;
	outResources->maxGeometryVaryingComponents = 64;
	outResources->maxTessControlInputComponents = 128;
	outResources->maxTessControlOutputComponents = 128;
	outResources->maxTessControlTextureImageUnits = 16;
	outResources->maxTessControlUniformComponents = 1024;
	outResources->maxTessControlTotalOutputComponents = 4096;
	outResources->maxTessEvaluationInputComponents = 128;
	outResources->maxTessEvaluationOutputComponents = 128;
	outResources->maxTessEvaluationTextureImageUnits = 16;
	outResources->maxTessEvaluationUniformComponents = 1024;
	outResources->maxTessPatchComponents = 120;
	outResources->maxPatchVertices = 32;
	outResources->maxTessGenLevel = 64;
	outResources->maxViewports = 16;
	outResources->maxVertexAtomicCounters = 0;
	outResources->maxTessControlAtomicCounters = 0;
	outResources->maxTessEvaluationAtomicCounters = 0;
	outResources->maxGeometryAtomicCounters = 0;
	outResources->maxFragmentAtomicCounters = 8;
	outResources->maxCombinedAtomicCounters = 8;
	outResources->maxAtomicCounterBindings = 1;
	outResources->maxVertexAtomicCounterBuffers = 0;
	outResources->maxTessControlAtomicCounterBuffers = 0;
	outResources->maxTessEvaluationAtomicCounterBuffers = 0;
	outResources->maxGeometryAtomicCounterBuffers = 0;
	outResources->maxFragmentAtomicCounterBuffers = 1;
	outResources->maxCombinedAtomicCounterBuffers = 1;
	outResources->maxAtomicCounterBufferSize = 16384;
	outResources->maxTransformFeedbackBuffers = 4;
	outResources->maxTransformFeedbackInterleavedComponents = 64;
	outResources->maxCullDistances = 8;
	outResources->maxCombinedClipAndCullDistances = 8;
	outResources->maxSamples = 4;
	outResources->limits.nonInductiveForLoops = 1;
	outResources->limits.whileLoops = 1;
	outResources->limits.doWhileLoops = 1;
	outResources->limits.generalUniformIndexing = 1;
	outResources->limits.generalAttributeMatrixVectorIndexing = 1;
	outResources->limits.generalVaryingIndexing = 1;
	outResources->limits.generalSamplerIndexing = 1;
	outResources->limits.generalVariableIndexing = 1;
	outResources->limits.generalConstantMatrixVectorIndexing = 1;
}

class ShaderIncluder : public glslang::TShader::Includer
{
private:
	const FileCache &headers;

public:
	ShaderIncluder(const FileCache &headers) : headers(headers) {}

	// Resolves an inclusion request by name, type, current source name,
	// and include depth.
	// On success, returns an IncludeResult containing the resolved name
	// and content of the include.  On failure, returns an IncludeResult
	// with an empty string for the file_name and error details in the
	// file_data field.  The Includer retains ownership of the contents
	// of the returned IncludeResult value, and those contents must
	// remain valid until the releaseInclude method is called on that
	// IncludeResult object.
	virtual IncludeResult* includeSystem(const char* requested_source, const char* requesting_source, size_t inclusion_depth)
	{
		const FileCache::const_iterator headerIt = headers.find(requested_source);

		if (headerIt != headers.cend())
		{
			return new IncludeResult(headerIt->first, headerIt->second.c_str(), headerIt->second.length(), nullptr);
		}

		return nullptr;
	}

	virtual IncludeResult* includeLocal(const char* requested_source, const char* requesting_source, size_t inclusion_depth)
	{
		return includeSystem(requested_source, requesting_source, inclusion_depth);
	}

	// Signals that the parser will no longer use the contents of the
	// specified IncludeResult.
	virtual void releaseInclude(IncludeResult* result)
	{
		delete result;
	}
};

const std::string GetOutputName(const std::string &source, const std::string &outputDir)
{
	const size_t lastSlash = source.find_last_of("\\/");
	std::string outputName;

	if (!outputDir.empty())
	{
		outputName = outputDir + "\\";
	}

	outputName += source.substr(lastSlash + 1);

	return outputName;
}

static bool IsUpToDate(const SourceFile &source, const std::string &outputFile, FileModifiedMap *inoutLastModified)
{
	const uint64_t sourceOutputTime = GetLastModifiedTime(outputFile + ".cpp", inoutLastModified);
	const uint64_t headerOutputTime = GetLastModifiedTime(outputFile + ".h", inoutLastModified);
	const uint64_t outputTime = std::max(sourceOutputTime, headerOutputTime);

	UpdateLastModifiedTime(source.includeFiles.data(), (unsigned)source.includeFiles.size(), inoutLastModified);

	if (GetLastModifiedTime(source.fileName, inoutLastModified) > outputTime)
	{
		return false;
	}

	for (size_t headerIndex = 0; headerIndex < source.includeFiles.size(); ++headerIndex)
	{
		if (GetLastModifiedTime(source.includeFiles[headerIndex], inoutLastModified) > outputTime)
		{
			return false;
		}
	}

	return true;
}

static bool CompileSourceFile(const SourceFile *optForceInclude, SourceFile *inoutSource, const TBuiltInResource &resources, const FileCache &headers, glslang::TProgram *outProgram, std::vector<unsigned> *outProgramDWords)
{
	glslang::TShader shader(inoutSource->stage);
	const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	ShaderIncluder includer(headers);
	std::vector<const char *> fileNames;
	std::vector<const char *> fileContents;
	std::vector<int> fileContentLengths;

	if (optForceInclude)
	{
		inoutSource->includeFiles.push_back(optForceInclude->fileName.c_str());
		inoutSource->includeFiles.insert(inoutSource->includeFiles.end(), optForceInclude->includeFiles.begin(), optForceInclude->includeFiles.end());

		fileNames.push_back(optForceInclude->fileName.c_str());
		fileContents.push_back(optForceInclude->contents.c_str());
		fileContentLengths.push_back(static_cast<int>(optForceInclude->contents.length()));
	}

	fileNames.push_back(inoutSource->fileName.c_str());
	fileContents.push_back(inoutSource->contents.c_str());
	fileContentLengths.push_back(static_cast<int>(inoutSource->contents.length()));

	shader.setStringsWithLengthsAndNames(fileContents.data(), fileContentLengths.data(), fileNames.data(), static_cast<int>(fileNames.size()));

	if (!shader.parse(&resources, 100, ECoreProfile, false, false, messages, includer))
	{
		std::cout << shader.getInfoLog() << std::endl << shader.getInfoDebugLog() << std::endl;
		std::cout << "Failed to parse \"" << inoutSource->fileName << "\". Excluding from build." << std::endl;
		return false;
	}

	outProgram->addShader(&shader);

	if (!outProgram->link(messages))
	{
		std::cout << outProgram->getInfoLog() << std::endl << outProgram->getInfoDebugLog() << std::endl;
		std::cout << "Failed to link \"" << inoutSource->fileName << "\". Excluding from build." << std::endl;
		return false;
	}

	glslang::SpvOptions buildOptions;
#ifdef NDEBUG
	buildOptions.generateDebugInfo = true; 
	buildOptions.disableOptimizer = true;
#else //#ifdef NDEBUG
	buildOptions.generateDebugInfo = false;
	buildOptions.disableOptimizer = false;
#endif //#else //#ifdef NDEBUG
	buildOptions.optimizeSize = false;
	buildOptions.disassemble = false;
	buildOptions.validate = true;

	glslang::GlslangToSpv(*outProgram->getIntermediate(inoutSource->stage), *outProgramDWords, &buildOptions);
	if (outProgramDWords->empty())
	{
		std::cout << "No binary data generated for \"" << inoutSource->fileName << "\". Excluding from build." << std::endl;
		return false;
	}
	else
	{
		outProgram->buildReflection(EShReflectionAllBlockVariables | EShReflectionIntermediateIO );
	}

	return true;
}

static const char* StageToReflectionStr(EShLanguage stage)
{
	switch (stage)
	{
		case EShLangVertex:         return "ShaderStageType::VERTEX";
		case EShLangTessControl:    return "ShaderStageType::TESS_CONTROL";
		case EShLangTessEvaluation: return "ShaderStageType::TESS_EVAL";
		case EShLangGeometry:       return "ShaderStageType::GEOMETRY";
		case EShLangFragment:       return "ShaderStageType::FRAGMENT";
		case EShLangCompute:        return "ShaderStageType::COMPUTE";
	}

	return "<ERROR -- UNKNOWN STAGE>";
}

static std::string FilenameToReflectionPrefix(const std::string& outputName)
{
	const size_t lastSlash = outputName.find_last_of("\\/");
	std::string prefix = outputName.substr(lastSlash + 1) + "_";

	for (char& ch : prefix)
	{
		if (ch == '.')
			ch = '_';
	}

	return prefix;
}

static bool ShaderProgramToC(const std::string& outputName, EShLanguage stage, glslang::TProgram& prog, const std::vector<unsigned> &progDWords)
{
	const std::string headerName = outputName + ".h";
	std::ofstream header(headerName, std::ios::out);

	if (!header)
	{
		std::cout << "Failed to open \"" << headerName.c_str() << "\" for writing." << std::endl;
		return false;
	}
	else
	{
		const std::string sourceName = outputName + ".cpp";
		std::ofstream source(sourceName, std::ios::out);

		if (!source)
		{
			std::cout << "Failed to open \"" << sourceName.c_str() << "\" for writing." << std::endl;
			return false;
		}
		else
		{
			const std::string prefix = FilenameToReflectionPrefix(outputName);

			header << "// Generated by GLSLToC. Do no modify." << std::endl;
			header << std::endl;
			header << "#pragma once" << std::endl;
			header << std::endl;
			header << "#include \"ShaderReflection.h\"" << std::endl;
			header << std::endl;

			source << "// Generated by GLSLToC. Do no modify." << std::endl;
			source << std::endl;
			source << "#include \"" << GetFilename(headerName.c_str()) << "\"" << std::endl;
			source << std::endl;

			header << "extern const ShaderModule " << prefix << "shader;" << std::endl;

			source << "static const unsigned " << prefix << "prog[] = {" << std::hex << std::endl;
			for (size_t progIndex = 0; progIndex < progDWords.size();)
			{
				source << "\t";
				for (size_t lineIndex = 0; lineIndex < 8 && progIndex < progDWords.size(); ++lineIndex, ++progIndex)
				{
					source << "0x" << std::setw(8) << std::setfill('0') << progDWords[progIndex];

					if (progIndex + 1 < progDWords.size())
						source << ", ";
				}

				source << std::endl;
			}
			source << "};" << std::dec << std::endl;
			source << std::endl;

			const int uboCount = prog.getNumUniformBlocks();

			source << "static const char * const " << prefix << "ubo_names[] = {" << std::endl;
			for (int uboIndex = 0; uboIndex < uboCount; ++uboIndex)
			{
				const glslang::TObjectReflection& ubo = prog.getUniformBlock(uboIndex);

				source << "\t\"" << ubo.name << "\"," << std::endl;
			}
			source << "\t\"\"" << std::endl;
			source << "};" << std::endl;
			source << std::endl;

			source << "static const ShaderUniformBlock " << prefix << "ubos[] = {" << std::endl;
			for (int uboIndex = 0; uboIndex < uboCount; ++uboIndex)
			{
				const glslang::TObjectReflection& ubo = prog.getUniformBlock(uboIndex);

				source << "\t{ " << ubo.getBinding() << ", " << ubo.size << " }," << std::endl;
			}
			source << "\t{}" << std::endl;
			source << "};" << std::endl;
			source << std::endl;

			source << "const ShaderModule " << prefix << "shader = {" << std::endl;
			source << "\t\"" << GetFilename(outputName.c_str()) << "\"," << std::endl;
			source << "\t\"main\"," << std::endl;
			source << "\t" << prefix << "prog," << std::endl;
			source << "\t" << prefix << "ubo_names," << std::endl;
			source << "\t" << prefix << "ubos," << std::endl;
			source << "\t" << progDWords.size() << "," << std::endl;
			source << "\t" << StageToReflectionStr(stage) << "," << std::endl;
			source << "\t" << prog.getNumUniformBlocks() << "," << std::endl;
			source << "\t" << prog.getNumPipeInputs() << "," << std::endl;
			source << "\t" << prog.getNumPipeOutputs() << std::endl;
			source << "};" << std::endl;
			source << std::endl;
		}
	}

	return true;
}

int main(int argc, char *argv[])
{
	Settings settings;
	FileCache headerCache;
	FileModifiedMap lastModifiedMap;
	SourceFile forceInclude;
	SourceFile *forceIncludePtr;
	TBuiltInResource resources;
	int ret;

	if (argc < 2 || !ParseSettings(argc, argv, &settings))
	{
		PrintHelp();
		return 0;
	}

	UpdateLastModifiedTime(settings.inputFiles.data(), (unsigned)settings.inputFiles.size(), &lastModifiedMap);

	if (!glslang::InitializeProcess())
	{
		std::cout << "Failed to initialize glslang." << std::endl;
		return -100;
	}

	InitResources(&resources);

	if (!settings.forceInclude.empty())
	{
		UpdateLastModifiedTime(&settings.forceInclude, 1, &lastModifiedMap);

		forceInclude.fileName.swap(settings.forceInclude);
		if (!LoadSourceFile(forceInclude.fileName, settings.includeDirs, &headerCache, &forceInclude.includeFiles, &forceInclude.contents))
		{
			std::cout << "Failed to load force include file." << std::endl;
			return -101;
		}

		forceIncludePtr = &forceInclude;
	}
	else
	{
		forceIncludePtr = nullptr;
	}

	ret = 0;
	std::vector<std::string> validPrograms;
	for (size_t sourceIndex = 0; sourceIndex < settings.inputFiles.size(); ++sourceIndex)
	{
		const std::string inputFilename = settings.inputFiles[sourceIndex];
 		std::string outputName = GetOutputName(inputFilename, settings.outputDir);
		SourceFile source;

		source.fileName.swap(settings.inputFiles[sourceIndex]);

		if (!LoadSourceFile(source.fileName, settings.includeDirs, &headerCache, &source.includeFiles, &source.contents))
		{
			--ret;
			std::cout << "Failed to load and parse \"" << source.fileName << "\". Exluding from build." << std::endl;
		}
		else
		{
			if (IsUpToDate(source, outputName, &lastModifiedMap))
			{
				validPrograms.emplace_back(std::move(outputName));
			}
			else
			{
				if (!DetermineStageFromFileName(source.fileName, &source.stage))
				{
					--ret;
				}
				else
				{
					glslang::TProgram shaderProgReflection;
					std::vector<unsigned> shaderProg;
					if (!CompileSourceFile(forceIncludePtr, &source, resources, headerCache, &shaderProgReflection, &shaderProg))
					{
						--ret;
						std::cout << "Failed to compile \"" << source.fileName << "\". Exluding from build." << std::endl;
					}
					else
					{
						if (ShaderProgramToC(outputName, source.stage, shaderProgReflection, shaderProg))
						{
							validPrograms.emplace_back(outputName);
						}
					}
				}
			}
		}
	}

	if (!validPrograms.empty())
	{
		std::string allFilename = settings.outputDir + "\\Shaders";
		std::ofstream allHeader(allFilename + ".h");
		std::ofstream allSource(allFilename + ".cpp");

		allHeader << "// Generated by GLSLToC. Do no modify." << std::endl;
		allHeader << std::endl;
		allHeader << "#pragma once" << std::endl;
		allHeader << std::endl;

		allHeader << "struct ShaderModule;" << std::endl;
		allHeader << std::endl;

		allHeader << "extern const unsigned shader_count;" << std::endl;
		allHeader << "extern const ShaderModule * const shaders[];" << std::endl;

		allSource << "// Generated by GLSLToC. Do no modify." << std::endl;
		allSource << std::endl;

		allSource << "#include \"Shaders.h\"" << std::endl;
		for (const std::string& file : validPrograms)
		{
			allSource << "#include \"" << GetFilename(file.c_str()) << ".h\"" << std::endl;
		}
		allSource << std::endl;

		allSource << "const unsigned shader_count = " << validPrograms.size() << ";" << std::endl;
		allSource << "const ShaderModule * const shaders[] = {" << std::endl;
		for (const std::string& file : validPrograms)
		{
			allSource << "\t&" << FilenameToReflectionPrefix(file) << "shader," << std::endl;
		}
		allSource << "\tnullptr" << std::endl;
		allSource << "};" << std::endl;
	}

	return ret;
}
