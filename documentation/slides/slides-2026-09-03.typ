#import "@preview/touying:0.7.4": *
#import themes.university: *

#show: university-theme.with(
  aspect-ratio: "16-9",
  config-info(
    title: [Computer project 2026 Group A],
    subtitle: [Subtitle],
    author: [
      Øyvind Nestvold,
      Fredrik Robertsen,
      Sivert Underdal,
      Vegard Kyrkjedelen Hovland,
      Mats Kvanvik,
      Mikal Samland-Johansen
    ],
    date: [2026-09-03],
    institution: [NTNU],
  ),
)

#title-slide[]

== Project description

#grid(
  columns: (1fr, 1fr),
  gutter: 1em,
  rows: 100%,
  align: horizon,
  [
    - Implement a Noita–like game with an automata based physics / interaction engine.
    - The player should be able to place, collide and interact with items / particles in the world.
  ],
  image("images/noita-background.png", height: 100%),
)



== Initial design requirements

- Targeting 720p, may pose challenges in terms of memory.
  - Fallback to 144p
  - An initial 720p model requires about 4MB of memory for the FPGA for the world representation alone.
- Player input via a typical console controller
  - Could implement our own.
- FPGA should be the sole physics simulator
  - Also handles screen buffer writes.
- MCU should handle user input, event handling and world state.
  - Needs a bidirectional SPI-Bus -> use MCU as master FPGA as slave.
- PCB should wire stuff...

== FPGA

- Vivado up and running on all team-members' computers
- Made example programs that interacts with input and output on board
- Planned tasks: SPI-implementation, try communicating with VGA port,
- calculate needed specifications on PCB for FPGA's sake.
- Will hold a meeting to further clarify design choices ahead of
  implementation.

== MCU

- MCU tutorial watched
- Began findig out what we need
- Figuring out what MCU we will land on
- Began looking at implementing SPI communication toward the FPGA

== PCB

- Done KiCad tutorial
- Started designing the powersupply as given in slides
- Worked on setting up collaboration via Git
