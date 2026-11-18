/* Desktop entry point and the SDL2 Android entry symbol. */
int wod_main(int argc, char **argv);

#ifdef __ANDROID__
int SDL_main(int argc, char **argv) {
    return wod_main(argc, argv);
}
#else
int main(int argc, char **argv) {
    return wod_main(argc, argv);
}
#endif
