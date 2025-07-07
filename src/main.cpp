#include <SDL2/SDL.h>
#include <DirManager/dirman.h>
#include "video_player.h"
extern "C"
{
#include "../res/noise.h"
}

static bool stopAlles = false;

static void videoLoop(const std::string &video, DerVideoPlayer& player, SDL_Renderer *render, SDL_Window *window)
{
    SDL_Event event;
    SDL_RWops *vFile;
    SDL_Log("Trying open video: %s", video.c_str());

    if(video == "noise")
        vFile = SDL_RWFromConstMem(noise_avi, noise_avi_size);
    else
        vFile = SDL_RWFromFile(video.c_str(), "rb");

    if(!vFile)
    {
        SDL_Log("Failed to open video file: %s", video.c_str());
        std::string msg = "Ich kann diese Video öffnen:\n" + video;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dieses Video ist Müll!", msg.c_str(), window);
        return;
    }

    if(!player.loadVideo(vFile, true))
    {
        SDL_RWclose(vFile);
        SDL_Log("Failed to open video: %s", video.c_str());
        std::string msg = "Ich kann diese Video öffnen:\n" + video;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dieses Video ist Müll!", msg.c_str(), window);
        return;
    }

    SDL_RenderClear(render);
    if(video != "noise")
        SDL_RenderPresent(render);

    SDL_PauseAudio(0);

    bool stop = false;

    while(!player.atEnd() && !stopAlles && !stop)
    {
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
                stopAlles = true;
            else if(event.type == SDL_KEYUP)
            {
                if(event.key.keysym.sym == SDLK_SPACE)
                    stop = true;
            }
        }

        if(player.hasVideoFrame())
        {
            SDL_RenderClear(render);
            player.drawVideoFrame();
            SDL_RenderPresent(render);
        }

        SDL_Delay(15);
    }

    SDL_PauseAudio(1);
}


int main()
{
    SDL_Window *window = nullptr;
    SDL_Renderer *render = nullptr;
    SDL_AudioSpec spec;
    SDL_AudioSpec obtained;

    DerVideoPlayer player;
    DirMan dir;

    dir.setPath("/home/vitaly/Видео/RPGMakerVideos");

    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER|SDL_INIT_EVENTS) < 0)
    {
        return 1;
    }

    window = SDL_CreateWindow("Sieh dir alle an!", 100, 100, 800, 600, SDL_WINDOW_SHOWN);
    if(!window)
    {
        SDL_Quit();
        return 1;
    }

    render = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(!render)
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    player.setRender(render);

    spec.format = AUDIO_S16SYS;
    spec.freq = 44100;
    spec.samples = 1024;
    spec.channels = 2;
    spec.callback = &DerVideoPlayer::audio_out_stream;
    spec.userdata = &player;

    SDL_OpenAudio(&spec, &obtained);
    player.setAudioSpec(obtained);


    dir.beginWalking({".avi", ".mpg"});
    std::string cur_path;
    std::vector<std::string> list;

    bool playNoise = true;

    while(dir.fetchListFromWalker(cur_path, list))
    {
        for(const auto &v : list)
        {
            if(stopAlles)
                break;

            videoLoop("noise", player, render, window);

            std::string vid = cur_path + "/" + v;
            videoLoop(vid, player, render, window);
        }
    }

    SDL_CloseAudio();
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
