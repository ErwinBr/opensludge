/*
 *  movie.h
 *  Sludge Engine
 *
 *  Created by Rikard Peterson on 2011-02-27.
 *  Copyright 2011 SLUDGE Developers. All rights reserved.
 *
 */


/*
 movieIsPlaying tracks the state of movie playing
 */
enum movieStates {
	nothing = 0,
	playing,
	paused
};
extern movieStates movieIsPlaying;

// Start playing a movie. The script that plays it waits until it has
// ended. Returns 0 when we can't play it.
int playMovie (int fileNumber);
// While a movie plays, the main loop calls this in each frame instead of
// drawing the game. We decode what the frame needs and show the frame of
// the movie that is due. Returns false when the movie has ended, after we
// clean up; the main loop then draws the game.
bool showMovieFrame ();
int stopMovie ();
int pauseMovie();
