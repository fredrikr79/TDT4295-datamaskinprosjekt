#import "@preview/touying:0.7.4": *
#import themes.university: *

#show: university-theme.with(
  aspect-ratio: "16-9",
  config-info(
    title: [Computer project 2026 Group A],
    subtitle: [],
    author: [
      Øyvind Nestvold,
      Fredrik Robertsen,
      Sivert Underdal,
      Vegard Kyrkjedelen Hovland,
      Mats Kvanvik,
      Mikal Samland-Johansen
    ],
    date: [2026-09-24],
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

== FPGA

- Octa xSPI WORKS when connected to the MCU
- VGA driver and simulator is fully set up
- Show demo

== MCU

- Also worked on the connection to the FPGA
- Have communicated with an SD-card
- Seen on USB, thinking of using tinyUSB

== PCB

- Lask week schematics where finalized
- Major components are in place, eg. VGA
