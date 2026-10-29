Torchlight II Stutter Fixes (GOG version)
========================================

Two parts. Both go in the game folder (the one with Torchlight2.exe).

1. tl2fix.exe - Patch your own game files. Your original files are saved as Torchlight2.exe.original and OgreMain.dll.original.
- Torchlight2.exe: character models (paperdoll) don't compare every vertex against every other vertex when welding armor seams. Now it checks by proximity which is a lot faster.
- OgreMain.dll: the game clock no longer pins the thread to one CPU core on every read which was causing uneven frame times.
   
The patcher identifies each file by its SHA-256 before changing anything and checks the results the same way.
If you used the earlier patched Torchlight2.exe that was passed around, tl2fix updates it: that version had a bug in its weight check (see below).

2. version.dll (tl2deflate) - Faster PAK compression/decompression by using libdeflate (about 2x faster).
Just drop it into the game folder. It's a byte-identical output and patches nothing on disk. 

On my machine stutter time per minute of play fell about 40%. Hitches of 50 ms or more went from about 3 per minute to under 1. tl2deflate halves the decompression part of what's left.

Compatibility
- GOG version only (Torchlight2.exe, May 2016). What you do with your gog exe is your business and I'm not trying to mess with steam drm here. 
- The OgreMain.dll fix also works on Steam (that file is identical), and tl2fix applies just that part.
- Nothing changes game logic or data. The fixes give identical results.
- Your antivirus might flag the version.dll because it stands in for a Windows DLL. Source is included in this repo.

About the earlier exe's bug
Somebody tried before to patch the vertex's weight check with Chat GPT by comparing against the address of the weight instead of the weight itself. So nearly every vertex counted as weighted. If you downloaded that this patcher also edits that version of the exe, which matched the original exactly in 600 test runs (both floating-point modes, damaged positions, edge distances).

As you can probably guess, Claude did all of the heavy lifting on this. I just made a logger to verify memory addresses and timestamp the clock functions. If you'd like to avoid the slop factor that makes perfect sense. It's worth mentioning the token usage was low, the source is included in the archive, and deep code dives that don't encroach on the work of artists is probably one of the less disruptive uses of AI. 

tl;dr: 
tl2fix contains targeted changes with only byte differences and no game code. 

Additional attribution:
MIT licence. libdeflate: MIT (Eric Biggers).
