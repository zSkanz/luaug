#include "luaug/platform/process.h"

#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

namespace luaug::platform {

ProcessResult runProcess(const std::vector<std::string>& arguments)
{
    ProcessResult result;
    if (arguments.empty())
        return result;
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments)
        argv.push_back(argument.c_str());
    argv.push_back(nullptr);

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr)
        return result;
    result.started = true;

    std::size_t size = 0;
    int exitCode = -1;
    // Reads until the program closes its output, then waits for it.
    void* data = SDL_ReadProcess(process, &size, &exitCode);
    if (data != nullptr) {
        result.output.assign(static_cast<const char*>(data), size);
        SDL_free(data);
    }
    result.exitCode = exitCode;
    SDL_DestroyProcess(process);
    return result;
}

} // namespace luaug::platform
