from focs._effects import Capital, EffectsGroup, IsSource, Planet, Unowned
from focs._species import *
from species.species_macros.empire_opinions import COMMON_OPINION_EFFECTS
from species.species_macros.env import NARROW_EP, TUNDRA_NARROW_EP
from species.species_macros.focus import (
    HAS_ADVANCED_FOCI,
    HAS_GROWTH_FOCUS,
    HAS_INDUSTRY_FOCUS,
    HAS_INFLUENCE_FOCUS,
    HAS_RESEARCH_FOCUS,
)
from species.species_macros.fuel import GREAT_FUEL
from species.species_macros.happiness import BAD_HAPPINESS
from species.species_macros.industry import AVERAGE_INDUSTRY
from species.species_macros.influence import AVERAGE_INFLUENCE
from species.species_macros.planet_defense import AVERAGE_PLANETARY_DEFENSE
from species.species_macros.planet_shields import AVERAGE_PLANETARY_SHIELDS
from species.species_macros.planet_size import LARGE_PLANET
from species.species_macros.population import AVERAGE_POPULATION
from species.species_macros.research import BAD_RESEARCH
from species.species_macros.shields import STANDARD_SHIP_SHIELDS
from species.species_macros.stockpile import GOOD_STOCKPILE
from species.species_macros.supply import AVERAGE_SUPPLY
from species.species_macros.telepathic import PRECOGNITIVE_DETECTION
from species.species_macros.troops import BAD_DEFENSE_TROOPS
from species.species_macros.weapons import GOOD_WEAPONS

Species(
    name="SP_FULVER",
    description="SP_FULVER_DESC",
    gameplay_description="SP_FULVER_GAMEPLAY_DESC",
    playable=True,
    can_produce_ships=True,
    can_colonize=True,
    tags=[
        "LITHIC",
        "GOOD_STOCKPILE",
        "GOOD_WEAPONS",
        "BAD_RESEARCH",
        "BAD_HAPPINESS",
        "AVERAGE_SUPPLY",
        "GREAT_FUEL",
        "TELEPATHIC",
        "PEDIA_LITHIC_SPECIES_CLASS",
        "PEDIA_TELEPATHIC_TITLE",
    ],
    foci=[
        HAS_INDUSTRY_FOCUS,
        HAS_RESEARCH_FOCUS,
        HAS_INFLUENCE_FOCUS,
        HAS_GROWTH_FOCUS,
        *HAS_ADVANCED_FOCI,
    ],
    defaultfocus="FOCUS_STOCKPILE",
    likes=[
        "BLD_HYPER_DAM",
        "BLD_STARGATE",
        "CRYSTALS_SPECIAL",
        "ECCENTRIC_ORBIT_SPECIAL",
        "FOCUS_STOCKPILE",
        "GAIA_SPECIAL",
        "MIMETIC_ALLOY_SPECIAL",
        "PLC_ALGORITHMIC_RESEARCH",
        "PLC_COLONIZATION",
        "PLC_CONFEDERATION",
        "PLC_DREAM_RECURSION",
        "PLC_ENGINEERING",
        "PLC_EXPLORATION",
        "PLC_LIBERTY",
        "PLC_MODERATION",
        "PLC_RACIAL_PURITY",
        "PLC_STOCKPILE_LIQUIDATION",
        "RESONANT_MOON_SPECIAL",
        "SUCCULENT_BARNACLES_SPECIAL",
    ],
    dislikes=[
        "ANCIENT_RUINS_DEPLETED_SPECIAL",
        "BLD_GAS_GIANT_GEN",
        "BLD_INDUSTRY_CENTER",
        "BLD_SHIPYARD_ENRG_COMP",
        "BLD_SHIPYARD_ENRG_SOLAR",
        "BLD_SOL_ORB_GEN",
        "FORTRESS_SPECIAL",
        "KRAKEN_NEST_SPECIAL",
        "PLC_BUREAUCRACY",
        "PLC_CHECKPOINTS",
        "PLC_ENVIRONMENTALISM",
        "PLC_INTERSTELLAR_INFRA",
        "PLC_SYSTEM_INFRA",
        "SUPERCONDUCTOR_SPECIAL",
    ],
    effectsgroups=[
        *AVERAGE_INDUSTRY,
        *BAD_RESEARCH,
        *AVERAGE_INFLUENCE,
        *GOOD_STOCKPILE,
        *AVERAGE_POPULATION,
        *BAD_HAPPINESS,
        COMMON_OPINION_EFFECTS("SP_FULVER"),
        *AVERAGE_SUPPLY,
        *BAD_DEFENSE_TROOPS,
        *GOOD_WEAPONS,
        *GREAT_FUEL,
        *PRECOGNITIVE_DETECTION(2),
        # not for description,
        *AVERAGE_PLANETARY_SHIELDS,
        *AVERAGE_PLANETARY_DEFENSE,
        *LARGE_PLANET,
        NARROW_EP,
        *STANDARD_SHIP_SHIELDS,
        EffectsGroup(
            scope=IsSource, activation=Planet() & ~Unowned & Capital, effects=GiveEmpirePolicy(name="PLC_CONFEDERATION")
        ),
    ],
    environments=TUNDRA_NARROW_EP,
    graphic="icons/species/insectoid-01.png",
)
