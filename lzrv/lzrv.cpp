
#include <pthread.h>
#include <string>
#include <iostream>
#include <SDL.h>
#include <lzr.h>

#define DEFAULT_WINDOW_SIZE 500


SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
Uint32 NEW_FRAME; //SDL event for new lzr frames

pthread_t zmq_thread;

pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
lzr_frame* frame;



//ZMQ recv loop
static void* loop_recv(void* data)
{
    void* zmq_ctx = lzr_create_zmq_ctx();
    void* rx      = lzr_create_frame_rx(zmq_ctx, LZR_ZMQ_ENDPOINT);
    lzr_frame* temp_frame = new lzr_frame;

    while(1)
    {
        std::cout << "waiting to recv" << std::endl;
        int r = lzr_recv_frame(rx, temp_frame);
        std::cout << "recv: " << r << std::endl;

        //since the recv is blocking, it *should* always
        //be true, but who knows...
        if(r > 0)
        {
            pthread_mutex_lock(&frame_lock);
            *frame = *temp_frame;
            pthread_mutex_unlock(&frame_lock);
        }
    }

    delete temp_frame;
    lzr_destroy_frame_rx(rx);
    lzr_destroy_zmq_ctx(zmq_ctx);
}

static inline int lzr_coord_to_screen(double v)
{
    return (int) (((v + 1.0) / 2.0) * DEFAULT_WINDOW_SIZE);
}

static void draw_frame()
{
    pthread_mutex_lock(&frame_lock);

    // clear the screen to black
    SDL_SetRenderDrawColor(renderer,
                           0,
                           0,
                           0,
                           255);
    SDL_RenderClear(renderer);

    for(size_t i = 0; i < (frame->n_points - 1); i++)
    {
        lzr_point p1 = frame->points[i];
        lzr_point p2 = frame->points[i+1];

        //TODO: double check that color is actually
        //      a property of the first point
        SDL_SetRenderDrawColor(renderer,
                               p1.r,
                               p1.g,
                               p1.b,
                               !LZR_POINT_IS_BLANKED(p1) * 255);

        SDL_RenderDrawLine(renderer,
                           lzr_coord_to_screen(p1.x),
                           lzr_coord_to_screen(p1.y),
                           lzr_coord_to_screen(p2.x),
                           lzr_coord_to_screen(p2.y));
    }

    pthread_mutex_unlock(&frame_lock);
}

static void loop()
{
    SDL_Event e;
    bool running = true;

    while(running)
    {
        std::cout << "loop" << std::endl;
        SDL_WaitEvent(NULL);

        //event pump
        while(SDL_PollEvent(&e))
        {
            switch(e.type)
            {
                //dispatch SDL events to their respective handlers
                case SDL_QUIT:        running = false; break;
                case SDL_WINDOWEVENT: break;
                case SDL_KEYDOWN:     break;
                default:
                    if(e.type == NEW_FRAME)
                    {
                        //draw the new lzr_frame
                        draw_frame();
                        SDL_RenderPresent(renderer);
                    }
            }
        }

        //cap at ~30 fps
        SDL_Delay(33);
    }
}


int main(int argc, char * argv[])
{
    frame = new lzr_frame;

    //start SDL
    if(SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL_Init Error" << std::endl;
        std::cerr << SDL_GetError() << std::endl;
        return EXIT_FAILURE;
    }

    window = SDL_CreateWindow("LZR Visualizer",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              DEFAULT_WINDOW_SIZE,
                              DEFAULT_WINDOW_SIZE,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if(window == NULL)
    {
        std::cerr << "SDL_CreateWindow Error" << std::endl;
        std::cerr << SDL_GetError() << std::endl;
        goto err_window;
    }

    renderer = SDL_CreateRenderer(window,
                                  -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(renderer == NULL)
    {
        std::cerr << "SDL_CreateRenderer Error" << std::endl;
        std::cerr << SDL_GetError() << std::endl;
        goto err_render;
    }

    NEW_FRAME = SDL_RegisterEvents(1);
    if(NEW_FRAME == ((Uint32) -1 ))
    {
        std::cerr << "Failed to register custom events" << std::endl;
        goto err_events;
    }


    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // clear the screen to black
    SDL_SetRenderDrawColor(renderer,
                           0,
                           0,
                           0,
                           255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);


    //start a ZMQ worker thread
    if(pthread_create(&zmq_thread, NULL, loop_recv, NULL))
    {
        std::cerr << "Failed to start ZMQ thread" << std::endl;
        goto err_thread;
    }

    //start the main loop
    loop();

    //stop ZMQ
    pthread_join(zmq_thread, NULL);


    //cleanup
err_events:
err_thread:
    SDL_DestroyRenderer(renderer);
err_render:
    SDL_DestroyWindow(window);
err_window:
    SDL_Quit();

    delete frame;

    return 0;
}
