package org.luaug.triangle;

import org.libsdl.app.SDLActivity;

/**
 * The one Java class this package adds. Everything else on the Java side is
 * SDL's own glue, compiled straight out of the vendored tree
 * (third_party/sdl3/android-project/app/src/main/java) rather than copied --
 * R13, and SDLActivity refuses to start when its version constants disagree
 * with the native library's, so a copy would eventually skew.
 *
 * <p>Two overrides, both for the same reason: this repository links SDL
 * statically and does not use SDL_main.
 */
public class TriangleActivity extends SDLActivity {

    /**
     * SDL3 is built static (third_party/CMakeLists.txt sets SDL_STATIC), so
     * there is no libSDL3.so to load -- the JNI entry points SDL's Java classes
     * call are inside libmain.so along with everything else. The default
     * implementation would try "SDL3" first and die with an UnsatisfiedLinkError
     * before reaching the one library that exists.
     */
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    /**
     * The sample has an ordinary {@code int main(int, char**)} and never
     * includes SDL_main.h, so the symbol SDL should dlsym() is "main" rather
     * than the default "SDL_main". Kept as an override here instead of a
     * {@code -Dmain=SDL_main} on the compile line, which would rewrite the
     * identifier in every header the translation unit pulls in as well.
     */
    @Override
    protected String getMainFunction() {
        return "main";
    }

    /**
     * No arguments. The sample's --headless / --verify / --screenshot flags all
     * require a readback and a frame budget, which is the CTest path; on a phone
     * the point is the window, and a human looking at it is the assertion.
     */
    @Override
    protected String[] getArguments() {
        return new String[0];
    }
}
