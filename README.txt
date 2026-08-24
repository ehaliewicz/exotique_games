---- Description ----

Basic riichi mahjong.  Currently doesn't detect win conditions.
Has basic AI, supports 4 player LAN multiplayer

Note: this is software rendered so should run fine without a discrete gpu, 
although it might have higher CPU usage than you expect.

---- Options ---- 
--host (runs in host mode, requires --num-clients, listens on all interfaces)
--num-clients [1-3] (declares number of expected clients, game won't start until all connect.  requires host mode)
--client [addr] (runs in client mode, attempts to connect to host at addr)
--no-dragon (uses a simpler wind indicator model, might help performance)
--no-music (disables music)
--no-aa (disables antialiasing)

---- Controls ----
Left/Right - select tile
A - draw
B - discard (or CANCEL CALL when game is waiting for CALL input(s))
X - call, riichi, or tsumo 
 - PON, CHII or RON a discarded tile if possible
 - discard selected tile and call RIICHI if possible
 - TSUMO is possible
Y - sort tiles (forward or reverse)



