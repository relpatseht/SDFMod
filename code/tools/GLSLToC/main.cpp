#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
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

static bool FindFile(const StringList *paths, std::string *inoutPath)
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

	for (size_t pathIndex = 0; pathIndex < paths->size(); ++pathIndex)
	{
		std::string testPath;

		testPath = (*paths)[pathIndex];
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
						std::cout << "\"" << fileBuf << "\" is not a valid directory." << std::endl;
						return false;
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
		if (!FindFile(&outSettings->includeDirs, &outSettings->forceInclude))
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

static uint64_t GetLastModifiedTime(const std::string *file, FileModifiedMap *map)
{
	const FileModifiedMap::const_iterator fileIt = map->find(*file);

	if (fileIt != map->cend())
	{
		return fileIt->second;
	}
	else
	{
		const HANDLE hFile = CreateFile(file->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		uint64_t time;

		time = 0;
		if (hFile != INVALID_HANDLE_VALUE)
		{
			FILETIME lastWrite;

			if (GetFileTime(hFile, nullptr, nullptr, &lastWrite))
			{
				time = (static_cast<uint64_t>(lastWrite.dwHighDateTime) << 32ULL) | lastWrite.dwLowDateTime;

				map->insert(std::make_pair(*file, time));
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
		const std::string * const file = files + fileIndex;

		GetLastModifiedTime(file, outMap);
	}
}

static bool LoadHeaderFile(const std::string *fileName, const std::string *fileCacheName, const StringList *includeDirs, StringList *outIncludeFiles, FileCache *fileCache);

static bool ParseIncludeDirective(const std::string *line, const StringList *includeDirs, StringList *outIncludeFiles, FileCache *fileCache)
{
	size_t includeStart;
	size_t includeEnd;
	std::string includeFile;
	std::string includeName;

	includeStart = line->find('"');
	if (includeStart == std::string::npos)
	{
		includeStart = line->find('<');
		includeEnd = line->find('>', includeStart + 1);
	}
	else
	{
		includeEnd = line->find('"', includeStart + 1);
	}

	if (includeStart == std::string::npos || includeEnd == std::string::npos)
	{
		std::cout << "Include directive \"" << line << "\" could not be parsed." << std::endl;
		return false;
	}

	includeName = line->substr(includeStart + 1, includeEnd - includeStart - 1);
	includeFile = includeName;
	if (!FindFile(includeDirs, &includeFile))
	{
		std::cout << "Include file \"" << includeFile << "\" not found." << std::endl;
		return false;
	}

	outIncludeFiles->push_back(includeFile);

	if(!LoadHeaderFile(&includeFile, &includeName, includeDirs, outIncludeFiles, fileCache))
	{
		std::cout << "Failed to load header file \"" << includeFile << "\"." << std::endl;
		return false;
	}

	return true;
}

static bool LoadFileFromStream(std::istream *stream, const StringList *includeDirs, FileCache *fileCache, StringList *outIncludeFiles, std::ostream *outFile)
{
	std::string line;

	while (std::getline(*stream, line))
	{
		if (line.find("#include ") == 0)
		{
			if (!ParseIncludeDirective(&line, includeDirs, outIncludeFiles, fileCache))
			{
				return false;
			}
		}

		(*outFile) << line << std::endl;
	}

	return true;
}

static bool LoadHeaderFile(const std::string *fileName, const std::string *fileCacheName, const StringList *includeDirs, StringList *outIncludeFiles, FileCache *fileCache)
{
	if (fileCache->count(*fileName) == 0)
	{
		std::ifstream file(fileName->c_str());
		std::ostringstream contents;

		if (!LoadFileFromStream(&file, includeDirs, fileCache, outIncludeFiles, &contents))
		{
			return false;
		}

		fileCache->insert(std::make_pair(*fileCacheName, std::move(contents.str())));
	}

	return true;
}

static bool LoadSourceFile(const std::string *fileName, StringList *includeDirs, FileCache *fileCache, StringList *outIncludeFiles, std::string *outFile)
{
	std::ifstream file(fileName->c_str());
	std::ostringstream contents;

	if (!LoadFileFromStream(&file, includeDirs, fileCache, outIncludeFiles, &contents))
	{
		return false;
	}

	outFile->swap(contents.str());
	
	return true;
}

static bool DetermineStageFromFileName(const std::string *fileName, EShLanguage *outStage)
{
	size_t extStart;

	extStart = fileName->rfind('.');
	if (extStart == std::string::npos)
	{
		std::cout << "No extension on \"" << fileName->c_str() << "\". Excluding from build." << std::endl;
		return false;
	}

	switch ((*fileName)[extStart + 1])
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
		
			ext = fileName->substr(extStart + 1);
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
			std::cout << "Unkown extension on \"" << fileName->c_str() << "\". Excluding from build." << std::endl;
			return false;
	}

	return true;
}

static void InitResources(TBuiltInResource *resources) 
{
	resources->maxLights = 32;
	resources->maxClipPlanes = 6;
	resources->maxTextureUnits = 32;
	resources->maxTextureCoords = 32;
	resources->maxVertexAttribs = 64;
	resources->maxVertexUniformComponents = 4096;
	resources->maxVaryingFloats = 64;
	resources->maxVertexTextureImageUnits = 32;
	resources->maxCombinedTextureImageUnits = 80;
	resources->maxTextureImageUnits = 32;
	resources->maxFragmentUniformComponents = 4096;
	resources->maxDrawBuffers = 32;
	resources->maxVertexUniformVectors = 128;
	resources->maxVaryingVectors = 8;
	resources->maxFragmentUniformVectors = 16;
	resources->maxVertexOutputVectors = 16;
	resources->maxFragmentInputVectors = 15;
	resources->minProgramTexelOffset = -8;
	resources->maxProgramTexelOffset = 7;
	resources->maxClipDistances = 8;
	resources->maxComputeWorkGroupCountX = 65535;
	resources->maxComputeWorkGroupCountY = 65535;
	resources->maxComputeWorkGroupCountZ = 65535;
	resources->maxComputeWorkGroupSizeX = 1024;
	resources->maxComputeWorkGroupSizeY = 1024;
	resources->maxComputeWorkGroupSizeZ = 64;
	resources->maxComputeUniformComponents = 1024;
	resources->maxComputeTextureImageUnits = 16;
	resources->maxComputeImageUniforms = 8;
	resources->maxComputeAtomicCounters = 8;
	resources->maxComputeAtomicCounterBuffers = 1;
	resources->maxVaryingComponents = 60;
	resources->maxVertexOutputComponents = 64;
	resources->maxGeometryInputComponents = 64;
	resources->maxGeometryOutputComponents = 128;
	resources->maxFragmentInputComponents = 128;
	resources->maxImageUnits = 8;
	resources->maxCombinedImageUnitsAndFragmentOutputs = 8;
	resources->maxCombinedShaderOutputResources = 8;
	resources->maxImageSamples = 0;
	resources->maxVertexImageUniforms = 0;
	resources->maxTessControlImageUniforms = 0;
	resources->maxTessEvaluationImageUniforms = 0;
	resources->maxGeometryImageUniforms = 0;
	resources->maxFragmentImageUniforms = 8;
	resources->maxCombinedImageUniforms = 8;
	resources->maxGeometryTextureImageUnits = 16;
	resources->maxGeometryOutputVertices = 256;
	resources->maxGeometryTotalOutputComponents = 1024;
	resources->maxGeometryUniformComponents = 1024;
	resources->maxGeometryVaryingComponents = 64;
	resources->maxTessControlInputComponents = 128;
	resources->maxTessControlOutputComponents = 128;
	resources->maxTessControlTextureImageUnits = 16;
	resources->maxTessControlUniformComponents = 1024;
	resources->maxTessControlTotalOutputComponents = 4096;
	resources->maxTessEvaluationInputComponents = 128;
	resources->maxTessEvaluationOutputComponents = 128;
	resources->maxTessEvaluationTextureImageUnits = 16;
	resources->maxTessEvaluationUniformComponents = 1024;
	resources->maxTessPatchComponents = 120;
	resources->maxPatchVertices = 32;
	resources->maxTessGenLevel = 64;
	resources->maxViewports = 16;
	resources->maxVertexAtomicCounters = 0;
	resources->maxTessControlAtomicCounters = 0;
	resources->maxTessEvaluationAtomicCounters = 0;
	resources->maxGeometryAtomicCounters = 0;
	resources->maxFragmentAtomicCounters = 8;
	resources->maxCombinedAtomicCounters = 8;
	resources->maxAtomicCounterBindings = 1;
	resources->maxVertexAtomicCounterBuffers = 0;
	resources->maxTessControlAtomicCounterBuffers = 0;
	resources->maxTessEvaluationAtomicCounterBuffers = 0;
	resources->maxGeometryAtomicCounterBuffers = 0;
	resources->maxFragmentAtomicCounterBuffers = 1;
	resources->maxCombinedAtomicCounterBuffers = 1;
	resources->maxAtomicCounterBufferSize = 16384;
	resources->maxTransformFeedbackBuffers = 4;
	resources->maxTransformFeedbackInterleavedComponents = 64;
	resources->maxCullDistances = 8;
	resources->maxCombinedClipAndCullDistances = 8;
	resources->maxSamples = 4;
	resources->limits.nonInductiveForLoops = 1;
	resources->limits.whileLoops = 1;
	resources->limits.doWhileLoops = 1;
	resources->limits.generalUniformIndexing = 1;
	resources->limits.generalAttributeMatrixVectorIndexing = 1;
	resources->limits.generalVaryingIndexing = 1;
	resources->limits.generalSamplerIndexing = 1;
	resources->limits.generalVariableIndexing = 1;
	resources->limits.generalConstantMatrixVectorIndexing = 1;
}

class ShaderIncluder : public glslang::TShader::Includer
{
private:
	const FileCache *headers;

public:
	ShaderIncluder(const FileCache *headers) : headers(headers) {}

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
		const FileCache::const_iterator headerIt = headers->find(requested_source);

		if (headerIt != headers->cend())
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

const std::string GetOutputName(const std::string *source, const std::string *outputDir)
{
	const size_t lastSlash = source->find_last_of("\\/");
	std::string outputName;

	if (!outputDir->empty())
	{
		outputName = *outputDir + "\\";
	}

	outputName += source->substr(lastSlash + 1) + ".shader";

	return outputName;
}

static bool IsUpToDate(const SourceFile *source, const std::string *outputFile, FileModifiedMap *lastModified)
{
	const uint64_t outputTime = GetLastModifiedTime(outputFile, lastModified);

	UpdateLastModifiedTime(source->includeFiles.data(), (unsigned)source->includeFiles.size(), lastModified);

	if (GetLastModifiedTime(&source->fileName, lastModified) > outputTime)
	{
		return false;
	}

	for (size_t headerIndex = 0; headerIndex < source->includeFiles.size(); ++headerIndex)
	{
		if (GetLastModifiedTime(&source->includeFiles[headerIndex], lastModified) > outputTime)
		{
			return false;
		}
	}

	return true;
}

static bool CompileSourceFile(const SourceFile *forceInclude, SourceFile *source, const TBuiltInResource *resources, const FileCache *headers, const std::string *outputDir, const std::string *decoration, FileModifiedMap *lastModifiedMap)
{
	glslang::TShader shader(source->stage);
	const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	const std::string outputName = GetOutputName(&source->fileName, outputDir);
	ShaderIncluder includer(headers);
	glslang::TProgram program;
	std::vector<unsigned> spvBytes;
	std::vector<const char *> fileNames;
	std::vector<const char *> fileContents;
	std::vector<int> fileContentLengths;

	if (forceInclude)
	{
		source->includeFiles.push_back(forceInclude->fileName.c_str());
		source->includeFiles.insert(source->includeFiles.end(), forceInclude->includeFiles.begin(), forceInclude->includeFiles.end());
	}

	if (IsUpToDate(source, &outputName, lastModifiedMap))
	{
		return true;
	}

	if(forceInclude)
	{
		fileNames.push_back(forceInclude->fileName.c_str());
		fileContents.push_back(forceInclude->contents.c_str());
		fileContentLengths.push_back(static_cast<int>(forceInclude->contents.length()));
	}

	fileNames.push_back(source->fileName.c_str());
	fileContents.push_back(source->contents.c_str());
	fileContentLengths.push_back(static_cast<int>(source->contents.length()));

	shader.setStringsWithLengthsAndNames(fileContents.data(), fileContentLengths.data(), fileNames.data(), static_cast<int>(fileNames.size()));

	if (!shader.parse(resources, 100, ECoreProfile, false, false, messages, includer))
	{
		std::cout << shader.getInfoLog() << std::endl << shader.getInfoDebugLog() << std::endl;
		std::cout << "Failed to parse \"" << source->fileName << "\". Excluding from build." << std::endl;
		return false;
	}

	program.addShader(&shader);

	if (!program.link(messages))
	{
		std::cout << program.getInfoLog() << std::endl << program.getInfoDebugLog() << std::endl;
		std::cout << "Failed to link \"" << source->fileName << "\". Excluding from build." << std::endl;
		return false;
	}

	glslang::GlslangToSpv(*program.getIntermediate(source->stage), spvBytes);
	if (spvBytes.empty())
	{
		std::cout << "No binary data generated for \"" << source->fileName << "\". Excluding from build." << std::endl;
		return false;
	}
	else
	{
		std::ofstream outputSource;

		program.buildReflection(EShReflectionSeparateBuffers | EShReflectionIntermediateIO);
		program.dumpReflection();

		outputSource.open(outputName);

		if (!outputSource.is_open())
		{
			std::cout << "Could not open \"" << outputName << "\" for writting." << std::endl;
			return false;
		}
		else
		{
			outputSource.write(reinterpret_cast<const char*>(spvBytes.data()), spvBytes.size()*sizeof(uint32_t));
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
		if (!LoadSourceFile(&forceInclude.fileName, &settings.includeDirs, &headerCache, &forceInclude.includeFiles, &forceInclude.contents))
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
	for (size_t sourceIndex = 0; sourceIndex < settings.inputFiles.size(); ++sourceIndex)
	{
		SourceFile source;

		source.fileName.swap(settings.inputFiles[sourceIndex]);

		if (!LoadSourceFile(&source.fileName, &settings.includeDirs, &headerCache, &source.includeFiles, &source.contents))
		{
			--ret;
			std::cout << "Failed to and parse \"" << source.fileName << "\". Exluding from build." << std::endl;
			continue;
		}

		if (!DetermineStageFromFileName(&source.fileName, &source.stage))
		{
			--ret;
			continue;
		}

		if (!CompileSourceFile(forceIncludePtr, &source, &resources, &headerCache, &settings.outputDir, &settings.decoration, &lastModifiedMap))
		{
			--ret;
			std::cout << "Failed to compile \"" << source.fileName << "\". Exluding from build." << std::endl;
			continue;
		}
	}

	return ret;
}
