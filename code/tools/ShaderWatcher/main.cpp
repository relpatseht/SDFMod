#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <thread>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <chrono>

namespace
{
	struct ci_char_traits : std::char_traits<char>
	{
		static bool eq(char a, char b) noexcept
		{
			return tolower(a) == tolower(b);
		}

		static bool lt(char a, char b) noexcept
		{
			return tolower(a) < tolower(b);
		}

		static bool ne(char a, char b) noexcept
		{
			return !eq(a, b);
		}

		static int compare(const char* s1, const char* s2, size_t n) {
			while (n-- != 0) 
			{
				if (toupper(*s1) < toupper(*s2)) return -1;
				if (toupper(*s1) > toupper(*s2)) return 1;
				++s1; ++s2;
			}
			return 0;
		}
		static const char* find(const char* s, int n, char a) {
			while (n-- > 0 && toupper(*s) != toupper(a)) 
			{
				++s;
			}
			return s;
		}
	};

	typedef std::basic_string<char, ci_char_traits> ci_string;

	namespace pcl
	{
		struct Settings
		{
			std::string inputDir;
			std::string outputDir;
			std::string glslToCPath;
			std::string clPath;
			std::string intermediateDir;
			std::string glslToCOptions;
			bool cleanRebuild;
		};

		static void PrintHelp()
		{
			std::cout << "ShaderWatcher" << std::endl;
			std::cout << "	Watches the shader directory for file changes and build data dlls on change." << std::endl;
			std::cout << std::endl;
			std::cout << "Usage:" << std::endl;
			std::cout << "	shaderwatcher [options] <input_dir> <output_dir> [glsltoc_options]" << std::endl;
			std::cout << std::endl;
			std::cout << "Description:" << std::endl;
			std::cout << "	Works with glsltoc to automatically build data dlls on any shader change," << std::endl;
			std::cout << "	such as a new file, rename, or save. Output dlls will be given the same" << std::endl;
			std::cout << "	base name as their input shader. Shaders that fail to build will have errors" << std::endl;
			std::cout << "	emit." << std::endl;
			std::cout << "	Only an input directory and an output directory are required, in that order." << std::endl;
			std::cout << "	If an intermediary directory is not specified, it will default to CWD. Any" << std::endl;
			std::cout << "	options after the output dir are passed on to glsltoc." << std::endl;
			std::cout << std::endl;
			std::cout << "Options:" << std::endl;
			std::cout << "	-I: Precedes an intermediary directory for source files and .obj files." << std::endl;
			std::cout << "	-R: Force a clean rebuild on start of all files in the directory." << std::endl;
			std::cout << "	-S: Precedes the path to the glsltoc compiler." << std::endl;
			std::cout << "	-C: Precedes the path to the cl compiler." << std::endl;
			std::cout << std::endl;
			exit(-1);
		}

		static void ValidateSettings(Settings* inoutSettings)
		{
			auto EnsureValidDir = [](std::string& pathStr)
			{
				std::filesystem::path path{ pathStr };
				std::filesystem::file_status stat = std::filesystem::status(path);

				if (!std::filesystem::exists(stat))
				{
					std::cout << "Path '" << pathStr << "' does not exist. Creating." << std::endl;
					if (!std::filesystem::create_directories(path))
					{
						std::cout << "Failed to create path '" << pathStr << "'" << std::endl;
						exit(-1);
					}

					stat = std::filesystem::status(path);
				}

				if (!std::filesystem::is_directory(stat))
				{
					std::cout << "Path '" << pathStr << "' is not a directory." << std::endl;
					PrintHelp();
				}

				pathStr = path.lexically_normal().string();
				const char lastCh = pathStr[pathStr.size() - 1];

				if (lastCh != std::filesystem::path::preferred_separator)
					pathStr += std::filesystem::path::preferred_separator;
			};
			auto EnsureValidCompiler = [](std::string &pathStr, const std::string & compiler)
			{
				std::filesystem::path path{ pathStr };

				if (std::filesystem::is_regular_file(path))
					path.remove_filename();

				path /= compiler;

				pathStr = path.lexically_normal().string();

				if (!std::filesystem::exists(path))
				{
					std::cout << "Compiler '" << pathStr << "' does not exist." << std::endl;
					PrintHelp();
				}
			};

			EnsureValidDir(inoutSettings->inputDir);
			EnsureValidDir(inoutSettings->outputDir);

			if (!inoutSettings->intermediateDir.empty())
				EnsureValidDir(inoutSettings->intermediateDir);
			else
			{
				inoutSettings->intermediateDir = '.';
				inoutSettings->intermediateDir += std::filesystem::path::preferred_separator;
			}

			EnsureValidCompiler(inoutSettings->glslToCPath, "glsltoc.exe");
			EnsureValidCompiler(inoutSettings->clPath, "cl.exe");
		}

		static Settings ParseCommandLine(int argc, char* argv[])
		{
			Settings settings;
			int arg;

			settings.cleanRebuild = false;
			 
			for (arg = 1; arg < argc; ++arg)
			{
				if (argv[arg][0] != '-')
					break;

				switch (argv[arg][1])
				{
				case 'I':
					if (arg + 1 >= argc || argv[arg+1][0] == '-')
					{
						std::cout << "Arg '-I' must be followed by a path." << std::endl;
						PrintHelp();
					}

					settings.intermediateDir = argv[++arg];
					break;
				case 'R':
					settings.cleanRebuild = true;
					break;
				case 'S':
					if (arg + 1 >= argc || argv[arg + 1][0] == '-')
					{
						std::cout << "Arg '-S' must be followed by a path." << std::endl;
						PrintHelp();
					}

					settings.glslToCPath = argv[++arg];
					break;
				case 'C':
					if (arg + 1 >= argc || argv[arg + 1][0] == '-')
					{
						std::cout << "Arg '-C' must be followed by a path." << std::endl;
						PrintHelp();
					}

					settings.clPath = argv[++arg];
					break;
				default:
					std::cout << "Unknown arg '" << argv[arg] << "'" << std::endl;
					PrintHelp();
				}
			}

			if (arg + 2 > argc)
				PrintHelp();

			settings.inputDir = argv[arg++];
			settings.outputDir = argv[arg++];

			while (arg < argc)
			{
				settings.glslToCOptions += argv[arg++];
				settings.glslToCOptions += " ";
			}

			ValidateSettings(&settings);

			return settings;
		}
	}

	namespace dir
	{
		static std::unordered_map<ci_string, size_t> ScanDir(const std::string& dir)
		{
			std::unordered_map<ci_string, size_t> files;

			for (const auto& p : std::filesystem::recursive_directory_iterator(dir))
			{
				if (p.is_regular_file())
				{
					files.emplace(p.path().string().c_str(), p.last_write_time().time_since_epoch().count());
				}
			}

			return files;
		}

		template<typename Callback>
		static bool WatchDir(const std::string& dir, const Callback &OnChange)
		{
			std::unordered_map<ci_string, size_t> oldFiles = ScanDir(dir);
			HANDLE change = FindFirstChangeNotification(dir.c_str(), true, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);

			if (change == INVALID_HANDLE_VALUE)
			{
				std::cout << "Failed to watch directory '" << dir << "' with " << GetLastError() << std::endl;
				return false;
			}

			for (;;)
			{
				DWORD waitStatus = WaitForSingleObject(change, INFINITE);

				std::this_thread::sleep_for(std::chrono::milliseconds(100));

				if (waitStatus == WAIT_OBJECT_0)
				{
					std::unordered_map<ci_string, size_t> curFiles = ScanDir(dir);
					std::vector<ci_string> newFiles, deletedFiles, modifiedFiles;

					// First do deletes and modifies (to better handle renames)
					for (const auto& oldFilePair : oldFiles)
					{
						const ci_string& file = oldFilePair.first;
						auto fileIt = curFiles.find(file);

						if (fileIt == curFiles.end())
							deletedFiles.emplace_back(file);
						else
						{
							const size_t fileLastWrite = oldFilePair.second;

							if (fileIt->second != fileLastWrite)
								modifiedFiles.emplace_back(file);
						}
					}

					for (const auto& filePair : curFiles)
					{
						auto oldFileIt = oldFiles.find(filePair.first);

						if (oldFileIt == oldFiles.end())
							newFiles.emplace_back(filePair.first);
					}

					oldFiles = std::move(curFiles);

					OnChange(newFiles, deletedFiles, modifiedFiles);
					std::this_thread::sleep_for(std::chrono::milliseconds(100));

					if (FindNextChangeNotification(change) == FALSE)
					{
						std::cout << "Failed to continue to watch directory '" << dir << "' with " << GetLastError() << std::endl;
						return false;
					}
				}
				else
				{
					std::cout << "Unknown wait status '" << waitStatus << "'. Failed with " << GetLastError() << std::endl;
					return false;
				}
			}

			return true;
		}
	}

	namespace proc
	{
		static bool RunCommand(const std::string& cmdLine, std::ostringstream* output)
		{
			HANDLE writePipe, readPipe;
			SECURITY_ATTRIBUTES sa = {};
			bool success = false;

			sa.nLength = sizeof(sa);
			sa.bInheritHandle = true;
			sa.lpSecurityDescriptor = nullptr;

			if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
			{
				std::cout << "Failed to create pipes to glsltoc process with " << GetLastError() << std::endl;
			}
			else
			{
				if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
				{
					std::cout << "Failed to disinherit the read pipe to glsltoc with " << GetLastError() << std::endl;
				}
				else
				{
					PROCESS_INFORMATION proc = {};
					STARTUPINFO si = {};
					char* const cmdLineCpy = (char*)_malloca((cmdLine.size() + 1) * sizeof(char));

					std::strcpy(cmdLineCpy, cmdLine.c_str());

					si.cb = sizeof(si);
					si.hStdError = writePipe;
					si.hStdOutput = writePipe;
					si.hStdInput = nullptr;
					si.dwFlags = STARTF_USESTDHANDLES;

					const bool procCreated = CreateProcess(nullptr, cmdLineCpy, nullptr, nullptr, true, 0, nullptr, nullptr, &si, &proc);

					CloseHandle(writePipe); // Close the write end of out pipe so the read returns when the process closes

					if (!procCreated)
						std::cout << "Failed to create glsltoc process using cmdline '" << cmdLine << "' with " << GetLastError() << std::endl;
					else
					{
						for(;;)
						{
							char buf[512];
							DWORD read = 0;
							const bool readSuccess = ReadFile(readPipe, buf, sizeof(buf - 1), &read, nullptr);

							if (!readSuccess || !read)
								break;

							buf[read] = '\0';
							(*output) << buf;

						} 

						success = true;
					}

					CloseHandle(proc.hThread);
					CloseHandle(proc.hProcess);
					_freea(cmdLineCpy);
				}

				CloseHandle(readPipe);
			}

			return success;
		}
	}

	namespace compile
	{
		namespace shaders
		{
			static bool CompileAll(const std::string &glslToC, const std::string &inDir, const std::string &outDir, const std::string &options, std::string *out)
			{
				std::ostringstream cmdLineBuild;
				std::ostringstream output;

				cmdLineBuild << "\"" << glslToC << "\" ";
				cmdLineBuild << "\"-O" << outDir << " \" ";
				cmdLineBuild << options << " ";
				cmdLineBuild << "\"" << inDir << " \"";

				if (proc::RunCommand(cmdLineBuild.str(), &output))
				{
					*out = output.str();
					return true;
				}

				return false;
			}
		}

		namespace cpp
		{
			static bool Compile(const std::string& cl, const ci_string& inFile, const std::string& outDir)
			{
				std::filesystem::path inPath{ inFile };
				std::filesystem::path outPath{ outDir };
				const std::string baseFileName = inPath.filename().replace_extension("").string();
				std::ostringstream output;
				std::ostringstream cmdLineBuild;

				outPath /= baseFileName + ".dll";

				cmdLineBuild << "\"" << cl << "\" ";
				cmdLineBuild << "/LD /D \"BUILD_DLL\" /D \"EXPORT_SHADER_SYM\" ";
				cmdLineBuild << "\"" << inFile.c_str() << "\" ";
				cmdLineBuild << "/link /NODEFAULTLIB /NOENTRY /MERGE:.rdata=.text /ALIGN:0x10 ";
				cmdLineBuild << "/OUT:\"" << outPath.string() << "\"";

				if (proc::RunCommand(cmdLineBuild.str(), &output))
				{
					std::cout << "Built '" << outPath.string() << "'" << std::endl;
					return true;
				}

				return false;
			}
		}
	}
}

int main(int argc, char* argv[])
{
	const pcl::Settings settings = pcl::ParseCommandLine(argc, argv);
	std::thread compileThread([&]()
	{
		dir::WatchDir(settings.intermediateDir, [&](const std::vector<ci_string>& newFiles, const std::vector<ci_string>& deletedFiles, const std::vector<ci_string>& modifiedFiles)
		{
			auto CompileCpp = [&](const std::vector<ci_string>& files)
			{
				for (const ci_string& file : files)
				{
					std::filesystem::path path{ file };
					const std::string ext = path.extension().string();

					if (ext == ".cpp")
					{
						if (!compile::cpp::Compile(settings.clPath, file, settings.outputDir))
						{
							std::cout << "Failed to build '" << file.c_str() << "'." << std::endl;
						}
					}
				}
			};

			for (const ci_string& file : deletedFiles)
			{
				std::filesystem::path path{ file };
				const ci_string ext = path.extension().string().c_str();

				if (ext == ".cpp")
				{
					std::filesystem::path outPath{ settings.outputDir };

					outPath /= path.filename();
					outPath.replace_extension(".dll");

					std::filesystem::remove(outPath);
				}
			}

			CompileCpp(newFiles);
			CompileCpp(modifiedFiles);
		});
	});
	
	if (settings.cleanRebuild)
	{
		std::string out;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		std::cout << "Force rebuilding all..." << std::endl;
		compile::shaders::CompileAll(settings.glslToCPath, settings.inputDir, settings.intermediateDir, settings.glslToCOptions + " -f", &out);

		std::cout << out << std::endl;
	}
	std::thread shaderThread([&]()
	{
		dir::WatchDir(settings.inputDir, [&](const std::vector<ci_string>&newFiles, const std::vector<ci_string>& deletedFiles, const std::vector<ci_string> &modifiedFiles)
		{
			if (!newFiles.empty() || !modifiedFiles.empty())
			{
				std::string out;

				compile::shaders::CompileAll(settings.glslToCPath, settings.inputDir, settings.intermediateDir, settings.glslToCOptions, &out);

				std::cout << out;
			}

			for (const ci_string& file : deletedFiles)
			{
				std::filesystem::path path{ file };
				const ci_string ext = path.extension().string().c_str();

				if (ext.starts_with(".f") || ext.starts_with(".v") || ext.starts_with(".g") || ext.starts_with(".c") || ext == ".tesc" || ext == ".tese")
				{
					std::filesystem::path outPath{ settings.intermediateDir };

					outPath /= path.filename();
					outPath.replace_extension(".cpp");

					std::filesystem::remove(outPath);
				}
			}
		});
	});

	compileThread.join();
	shaderThread.join();

	return 0;
}
