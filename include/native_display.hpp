#pragma once
#include <SDL.h>
#include <cstdint>
namespace uu {struct Picture;}
namespace uu::display {
bool nativeRequested();
SDL_Renderer *createRenderer(SDL_Window *&,int,int);
bool active();
void shutdown();
void setColor(SDL_Renderer *,Uint8,Uint8,Uint8,Uint8);
int fillRect(SDL_Renderer *,const SDL_Rect *);
int clear(SDL_Renderer *);
void present(SDL_Renderer *);
SDL_Texture *textureFromSurface(SDL_Renderer *,SDL_Surface *);
void destroyTexture(SDL_Texture *);
int copy(SDL_Renderer *,SDL_Texture *,const SDL_Rect *,const SDL_Rect *);
int vsync(SDL_Renderer *,int);
bool acceptVideo(const Picture &);
void drawVideo(const SDL_Rect &);
void resetVideo();
uint64_t directFrames();
uint64_t mapHits();
uint64_t mapImports();
uint64_t mapCached();
}
