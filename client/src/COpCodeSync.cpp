#include "CHighStakesRace.h"
///////////////////////////////////////////////////////////////////////////
// Tornamic 2025-03-02:                                                  //
//                                                                       //
//    READ BEFORE LEARNING THIS CODE:                                    //
//                                                                       //
//    DON'T SCROLL DOWN, RUN AWAY                                        //
//    I'VE BEEN WRITING THIS FOR A MONTH NOW                             //
//    DON'T ASK ME HOW IT WORKS I DON'T EVEN KNOW ANYMORE                //
//    IF YOU FIND A BUG, IT'S A FEATURE                                  //
//    IF IT WORKS, DON'T TOUCH IT                                        //
//    IF IT DOESN'T WORK, PRETEND IT DOES                                //
//    GOOD LUCK                                                          //
//                                                                       //
//    WARNING:                                                           //
//    - ANY ATTEMPT TO UNDERSTAND THIS CODE MAY CAUSE HEADACHES          //
//    - DEBUGGING THIS MAY LEAD TO EXISTENTIAL CRISIS                    //
//    - REFACTORING IS CONSIDERED A HIGH-RISK OPERATION                  //
//    - IF YOU FIX ONE BUG, THREE NEW ONES WILL APPEAR                   //
//    - COMMENTS ARE FOR WEAKLINGS, REAL WARRIORS USE TRIAL AND ERROR    //
//                                                                       //
//    REMINDER:                                                          //
//    - DON'T TRY TO UNDERSTAND THE LOGIC, JUST ACCEPT IT                //
//    - IF THIS CODE COMPILES, WE ALL WIN                                //
//    - IF YOU CAN'T FIX IT, JUST YELL AT IT AND RESTART                 //
//                                                                       //
//    P.S. IF YOU'RE STILL READING THIS, SEEK HELP                       //
//    P.S.2 DON'T EVEN THINK ABOUT CHANGING A SINGLE LINE                //
//    P.S.3 I WARNED YOU                                                 //
//    P.S.4 YOU'VE ALREADY SPENT TOO MUCH TIME HERE                      //
//    P.S.5 TIME TO ACCEPT THE MADNESS                                   //
//                                                                       //
///////////////////////////////////////////////////////////////////////////

#include "eScriptCommands.h"
#include "stdafx.h"
#include "CMissionLock.h"
#include "CCoopPool.h"
#include "CActivityChallenge.h"
#include "CMissionMode.h"
#include "CMissionSyncRules.h"
#include "COpCodeSync.h"
#include "CCutsceneKeepSeat.h"
#include <CUserDisplay.h>
#include <Commands/CCustomCommandMgr.h>
#include "CEntryExitMarkerSync.h"
#include <CTaskSequenceSync.h>
#include <CNetworkAnimQueue.h>
#include <CCutsceneSkipVote.h>
#include <Hooks/MissionAudioHooks.h>
#include "CCameraMirror.h"
#include "CPatientHandover.h"
#include <CCoopMission.h>
#include <CMissionMirror.h>
#include <map>

// Keep sorted!
const SSyncedOpCode syncedOpcodes[] = {
    // Messages
    {0x00BA},  // print_big {key} [gxt_key] {time} [int] {style} [TextStyle]
    {0x00BE},  // clear_prints
    {0x01E3},  // print_with_number_big {key} [gxt_key] {num} [int] {duration} [int] {style}[TextStyle]
    {COMMAND_PRINT},                       // {key} [gxt_key] {duration} [int] {flag} [int]
    {COMMAND_PRINT_NOW},                   // {key} [gxt_key] {duration} [int] {flag} [int]
    {COMMAND_PRINT_WITH_NUMBER_NOW},       // {key} [gxt_key] {num} [int] {duration} [int] {flag} [int]
    {COMMAND_PRINT_STRING_IN_STRING_NOW},  // {key} [gxt_key] {key2} [gxt_key] {duration} [int] {style} [TextStyle]
    // Mission mirror layer (CMissionMirror): the other texts. The authority sends them for its mission family (also from
    // scripts that are not registered, e.g. the AUDIOL/A_CONT dialogue helpers); a receiver drops a replayed opcode that is
    // not in this table.
    {0x01E4},  // print_with_number {key} [gxt_key] {num} [int] {duration} [int] {flag} [int]
    {0x0217},  // print_big_q {key} [gxt_key] {duration} [int] {style} [TextStyle]
    {0x02FF},  // print_with_3_numbers
    {0x0302},  // print_with_4_numbers
    {0x0303},  // print_with_4_numbers_now
    {0x0308},  // print_with_6_numbers
    {0x036D},  // print_with_2_numbers_big
    {0x03E7},  // flash_hud_object {object} [HudObject]
    {0x03EB},  // clear_small_prints
    {0x0513},  // print_help_forever_with_number {gxt} [gxt_key] {number} [int]
    {0x09C1},  // add_next_message_to_previous_briefs {state} [bool]
    {0x0A0E},  // clear_this_print_big_now {textStyle} [TextStyle]
    {0x0318},  // register_mission_passed {key} [gxt_key]
    {0x03E5},  // print_help {key} [gxt_key]
    {0x054C},  // load_mission_text {tableName} [string]
    {0x0998},  // award_player_mission_respect {value} [int]
    // Audio: the zones that play a mission's music (MUSIC1 'BEACH' party, SWEET6 'LOWRIDE' meet). T97, mission-audio-zones
    {0x0917},  // switch_audio_zone {zone} [string] {state} [bool]

    // Population management
    {0x01EB},  // set_car_density_multiplier {multiplier} [float]
    {0x0395},  // clear_area {x} [float] {y} [float] {z} [float] {radius} [float] {clearParticles} [bool]
    {0x03DE},  // set_ped_density_multiplier {multiplier} [float]
    {0x09D2},  // enable_ambient_crime {state} [bool]
    {0x06D0},  // switch_emergency_services {state} [bool]
    {0x014C},  // switch_car_generator[CarGenerator] {amount} [int]

    // Camera
    {0x015F},  // set_fixed_camera_position {x} [float] {y} [float] {z} [float] {xRotation} [float] {yRotation} [float]
               // {zRotation} [float]
    {0x0160},  // point_camera_at_point {x} [float] {y} [float] {z} [float] {switchStyle} [SwitchType]
    {0x02EB},  // restore_camera_jumpcut
    {0x0373},  // set_camera_behind_player
    {0x0920},  // camera_set_vector_track {fromX} [float] {fromY} [float] {fromZ} [float] {toX} [float] {toY} [float]
               // {toZ} [float] {time} [int] {ease} [bool]
    {0x0925},  // camera_reset_new_scriptables
    {0x092F},  // camera_persist_track {state} [bool]
    {0x0930},  // camera_persist_pos {state} [bool]
    {0x0936},  // camera_set_vector_move {fromX} [float] {fromY} [float] {fromZ} [float] {toX} [float] {toY} [float]
               // {toZ} [float] {time} [int] {ease} [bool]
    {0x0A0B},  // load_scene_in_direction {x} [float] {y} [float] {z} [float] {heading} [float]
    {COMMAND_POINT_CAMERA_AT_CHAR, true, {eSyncedParamType::PED}},  // [Char] {mode} [CameraMode] {switchStyle}
    {COMMAND_POINT_CAMERA_AT_CAR, true, {eSyncedParamType::VEHICLE}},  // [Car] {mode} [CameraMode] {switchStyle}
    {COMMAND_ATTACH_CAMERA_TO_VEHICLE, true, {eSyncedParamType::VEHICLE}},  // [Car] {offsets} {rotation} {tilt} {switchStyle}
    {COMMAND_RESTORE_CAMERA},
    {COMMAND_SET_FADING_COLOUR},  // {r} {g} {b}
    {COMMAND_SET_INTERPOLATION_PARAMETERS},  // {delay} {time}
    {COMMAND_SHAKE_CAM},  // {intensity}
    {COMMAND_CAMERA_SET_SHAKE_SIMULATION_SIMPLE},  // {type} {duration} {intensity}
    {COMMAND_CAMERA_SET_LERP_FOV},  // {from} {to} {time} {ease}
    {COMMAND_CAMERA_PERSIST_FOV},  // {state}
    {COMMAND_SET_DARKNESS_EFFECT},  // {state} {pedsAlpha}
    {COMMAND_SET_TIME_SCALE},  // {scale}, slow motion scenes
    {COMMAND_ADD_SPARKS},  // {x} {y} {z} {velocity} {density}
    {COMMAND_SET_CAR_HYDRAULICS, true, {eSyncedParamType::VEHICLE}},  // [Car] {state}
    {COMMAND_SET_PLAYER_DRUNKENNESS, true, {eSyncedParamType::PLAYER}},

    // Scenes
    {0x016A},  // do_fade {time} [int] {direction} [Fade]
    {0x02A3},  // switch_widescreen {state} [bool]
    {0x02E4},  // load_cutscene {name} [string]
    {0x02E7},  // start_cutscene
    {0x02EA},  // clear_cutscene
    {COMMAND_APPEND_TO_NEXT_CUTSCENE},  // cutscenes made of several parts

    // World
    {COMMAND_SET_TAG_STATUS_IN_AREA},
    {COMMAND_SET_WANTED_MULTIPLIER},  // missions slow the stars down (Sweet's Girl: 0.2); every player gets the same factor
    {COMMAND_INCREMENT_FLOAT_STAT_NO_MESSAGE},  // {stat} {amount}: the Paramedic reward (max health) reaches every player of the mission
    {COMMAND_SET_INT_STAT},  // only The Green Sabre's end of Los Santos (cities passed, Badlands started), see IsOpcodeSyncable

    // Interiors
    {0x04BB},  // set_area_visible {areaId} [Interior]
               //{0x07FB}, // switch_entry_exit {interiorName} [string] {state} [bool]
    {COMMAND_ACTIVATE_INTERIOR_PEDS},

    // Weapons
    {0x06AB, true, {eSyncedParamType::PED}},  // hide_char_weapon_for_scripted_cutscene [Char] {state} [bool]
    {0x07A7, true, {eSyncedParamType::PED}},  // task_jetpack {handle} [Char]
    {COMMAND_GIVE_WEAPON_TO_CHAR, true, {eSyncedParamType::PED}},
    {COMMAND_SET_CURRENT_CHAR_WEAPON, true, {eSyncedParamType::PED}},
    {COMMAND_REMOVE_WEAPON_FROM_CHAR, true, {eSyncedParamType::PED}},

    // Tasks
    {0x05BA, true, {eSyncedParamType::PED}},  // task_stand_still {handle} [Char] {time} [int]
    {0x05BF, true,
        {eSyncedParamType::PED,
            eSyncedParamType::PED}},  // task_look_at_char {observer} [Char] {target} [Char] {time} [int]
    {0x05C0, true,
        {eSyncedParamType::PED,
            eSyncedParamType::VEHICLE}},  // task_look_at_vehicle {char} [Char] {vehicle} [Car] {time} [int]
    {0x05CA, true, {eSyncedParamType::PED, eSyncedParamType::VEHICLE}},  // task_enter_car_as_passenger {char} [Char]
                                                                         // {vehicle} [Car] {time} [int] {seatId} [int]
    {0x05CB, true,
        {eSyncedParamType::PED,
            eSyncedParamType::VEHICLE}},      // task_enter_car_as_driver {char} [Char] {vehicle} [Car] {time} [int]
    {0x0603, true, {eSyncedParamType::PED}},  // task_go_to_coord_any_means {char} [Char] {x} [float] {y} [float] {z}
                                              // [float] {walkSpeed} [int] {vehicle} [Car] // OOPS, 6th parameter is an
                                              // entity, but its not used anywhere
    {0x0634, true,
        {eSyncedParamType::PED,
            eSyncedParamType::PED}},  // task_kill_char_on_foot_while_ducking {char} [Char] {target} [Char] {flags}
                                      // [int] {actionDelay} [int] {actionChance} [int]
    {0x0673, true, {eSyncedParamType::PED}},  // task_dive_and_get_up {handle} [Char] {directionX} [float] {directionY}
                                              // [float] {timeOnGround} [int]
    {0x0713, true,
        {eSyncedParamType::PED, eSyncedParamType::PED,
            eSyncedParamType::VEHICLE}},  // task_drive_by {handle} [Char] {targetChar} [Char] {targetVehicle} [Car] {x}
                                          // [float] {y} [float] {z} [float] {radius} [float] {type} [DriveByType]
                                          // {rightHandCarSeat} [bool] {fireRate} [int]
    {0x0792, true, {eSyncedParamType::PED}},  // clear_char_tasks_immediately [Char]
    {0x0967, true, {eSyncedParamType::PED}},  // start_char_facial_talk [Char] {duration} [int]
    {0x0968, true, {eSyncedParamType::PED}},  // stop_char_facial_talk [Char]
    {COMMAND_TASK_CHAT_WITH_CHAR, true, {eSyncedParamType::PED, eSyncedParamType::PED}},  // [Char] [Char] {leadSpeaker} [bool] {unk}
    {COMMAND_TASK_PLAY_ANIM_NON_INTERRUPTABLE, true, eSyncedParamType::PED},
    {COMMAND_TASK_GO_STRAIGHT_TO_COORD, true, eSyncedParamType::PED},
    {COMMAND_TASK_ACHIEVE_HEADING, true, eSyncedParamType::PED}, {COMMAND_TASK_JUMP, true, eSyncedParamType::PED},
    {COMMAND_TASK_TIRED, true, eSyncedParamType::PED}, {COMMAND_TASK_PLAY_ANIM_SECONDARY, true, eSyncedParamType::PED},
    {COMMAND_TASK_CHAR_SLIDE_TO_COORD_AND_PLAY_ANIM, true, eSyncedParamType::PED},
    {COMMAND_TASK_PLAY_ANIM, true, eSyncedParamType::PED},
    {COMMAND_TASK_TURN_CHAR_TO_FACE_CHAR, true, eSyncedParamType::PED, eSyncedParamType::PED},
    {COMMAND_TASK_SHOOT_AT_COORD, true, eSyncedParamType::PED},
    {COMMAND_TASK_LOOK_AT_CHAR, true, eSyncedParamType::PED, eSyncedParamType::PED},
    {COMMAND_CLEAR_CHAR_TASKS, true, eSyncedParamType::PED}, {COMMAND_CLEAR_LOOK_AT, true, eSyncedParamType::PED},
    {COMMAND_TASK_STAND_STILL, true, eSyncedParamType::PED}, {COMMAND_TASK_LOOK_AT_COORD, true, eSyncedParamType::PED},
    {COMMAND_TASK_LEAVE_CAR, true, eSyncedParamType::PED, eSyncedParamType::VEHICLE},
    {COMMAND_TASK_DIE_NAMED_ANIM, true, eSyncedParamType::PED},
    {COMMAND_TASK_PLAY_ANIM_WITH_FLAGS, true, eSyncedParamType::PED},
    {COMMAND_TASK_LEAVE_ANY_CAR, true, eSyncedParamType::PED}, {COMMAND_TASK_LOOK_ABOUT, true, eSyncedParamType::PED},
    {COMMAND_TASK_WANDER_STANDARD, true, eSyncedParamType::PED}, 
    {COMMAND_TASK_CLIMB, true, {eSyncedParamType::PED}},
    {COMMAND_FLUSH_ROUTE}, 
    {COMMAND_EXTEND_ROUTE},
    {COMMAND_TASK_FOLLOW_POINT_ROUTE, true, {eSyncedParamType::PED}},

    // Actors
    {0x00A1, true, {eSyncedParamType::PED}},  // set_char_coordinates [Char] {x} [float] {y} [float] {z} [float]
    {0x0173, true, {eSyncedParamType::PED}},  // set_char_heading [Char] {heading} [float]
    {0x0350, true, {eSyncedParamType::PED}},  // set_char_stay_in_same_place [Char] {state} [bool]
    {0x07A1},                                 // set_next_desired_move_state {moveState} [MoveState]
    {0x0245, true, {eSyncedParamType::PED}},  // set_anim_group_for_char[Char] {animGroup} [AnimGroup]
    {0x0860, true, {eSyncedParamType::PED}},  // {0860:} set_char_area_visible [Char] {interiorId} [int]

    // Vehicles
    {0x00AB, true, {eSyncedParamType::VEHICLE}},  // set_car_coordinates [Car] {x} [float] {y} [float] {z} [float]
    {0x0175, true, {eSyncedParamType::VEHICLE}},  // set_car_heading [Car] {heading} [float]
    {COMMAND_WARP_CHAR_FROM_CAR_TO_COORD, true, {eSyncedParamType::PED}},
    {COMMAND_WARP_CHAR_INTO_CAR, true, {eSyncedParamType::PED, eSyncedParamType::VEHICLE}},
    {COMMAND_WARP_CHAR_INTO_CAR_AS_PASSENGER, true, {eSyncedParamType::PED, eSyncedParamType::VEHICLE}},

    // Explosions
    {0x070C, true, {eSyncedParamType::VEHICLE}},  // explode_car_in_cutscene [Car]
    // the host's script only runs on the host: forward the "boom" so guests see the same explosion, particles,
    // shake and damage (the projectile/car explosions of the simulation are simulated by every client already)
    {COMMAND_ADD_EXPLOSION},  // add_explosion {type} {x} {y} {z}
    {COMMAND_ADD_EXPLOSION_NO_SOUND},  // add_explosion_no_sound {type} {x} {y} {z}
    {COMMAND_ADD_EXPLOSION_VARIABLE_SHAKE},  // add_explosion_variable_shake {x} {y} {z} {type} {shake}
    {COMMAND_EXPLODE_CHAR_HEAD, true, {eSyncedParamType::PED}},  // explode_char_head [Char]
    // COMMAND_EXPLODE_CAR (0x020B) is NOT synced on purpose: the vehicle health is synced (PacketHandlers/
    // vehicles.cpp writes m_fHealth from the network) and the game blows the car up on every client when it
    // reaches 0, so replaying the opcode would explode the same car twice (two booms, double damage).

    // Audio
    {0x097A},  // report_mission_audio_event_at_position {x} [float] {y} [float] {z} [float] {soundId} [int]
    {COMMAND_CLEAR_MISSION_AUDIO},  // {slot}, an interrupted dialogue stops for everyone

    // Animations
    {0x04ED},                                 // request_animation {animationFile} [string]
    {0x04EF},                                 // remove_animation {animationFile} [string]
    {0x0605, true, {eSyncedParamType::PED}},  // task_play_anim {handle} [Char] {animationName} [string] {animationFile}
                                              // [string] {frameDelta} [float] {loop} [bool] {lockX} [bool] {lockY}
                                              // [bool] {lockF} [bool] {time} [int]

    // Controls
    {0x01B4, true, {eSyncedParamType::PLAYER}},  // set_player_control[Player] {state}[bool]
    {COMMAND_ADD_SCORE, true, {eSyncedParamType::PLAYER}},
    {COMMAND_CLEAR_WANTED_LEVEL, true, {eSyncedParamType::PLAYER}},
    // stars a coop mission gives (Drive-By: 2 after the Ballas): each player gets them on his own wanted level
    {COMMAND_ALTER_WANTED_LEVEL, true, {eSyncedParamType::PLAYER}},
    {COMMAND_ALTER_WANTED_LEVEL_NO_DROP, true, {eSyncedParamType::PLAYER}},

    // Scene extras (visual/audio only, collected from the coop mission scenes)
    {COMMAND_LOAD_SCENE},  // {x} {y} {z}
    {COMMAND_REQUEST_COLLISION},  // {x} {y}
    {COMMAND_SET_NEAR_CLIP},  // {distance}
    {COMMAND_SET_CAMERA_ZOOM},  // {mode}
    {COMMAND_SET_CAMERA_IN_FRONT_OF_CHAR, true, {eSyncedParamType::PED}},  // [Char]
    {COMMAND_ATTACH_CAMERA_TO_CHAR, true, {eSyncedParamType::PED}},  // [Char] {offsets} {rotation} {tilt} {switchStyle}
    {COMMAND_DO_CAMERA_BUMP},  // {xOffset} {yOffset}
    {COMMAND_SET_CAMERA_POSITION_UNFIXED},  // {xOffset} {yOffset}
    {COMMAND_ALLOW_FIXED_CAMERA_COLLISION},  // {state}
    {COMMAND_DISPLAY_RADAR},  // {state}
    {COMMAND_DISPLAY_HUD},  // {state}
    {COMMAND_DISPLAY_ZONE_NAMES},  // {state}
    {COMMAND_DISPLAY_CAR_NAMES},  // {state}
    {COMMAND_SET_HEATHAZE_EFFECT},  // {state}
    {COMMAND_SET_EXTRA_COLOURS},  // {color} {fade}
    {COMMAND_CLEAR_EXTRA_COLOURS},  // {withFade}
    {COMMAND_DRAW_SUBTITLES_BEFORE_FADE},  // {state}
    {COMMAND_DRAW_ODDJOB_TITLE_BEFORE_FADE},  // {state}
    {COMMAND_CLEAR_THIS_PRINT},  // {key} [gxt_key]
    {COMMAND_CLEAR_THIS_BIG_PRINT},  // {key} [gxt_key]
    {COMMAND_PRINT_HELP_FOREVER},  // {key} [gxt_key]
    {COMMAND_CLEAR_HELP},
    {COMMAND_PRINT_WITH_2_NUMBERS_NOW},  // {key} {num1} {num2} {duration} {style}
    {COMMAND_PRINT_WITH_2_NUMBERS_BIG},  // {key} {num1} {num2} {duration} {style}
    {COMMAND_PLAY_MISSION_PASSED_TUNE},  // {soundId}
    {COMMAND_SET_MUSIC_DOES_FADE},  // {state}
    {COMMAND_ADD_ONE_OFF_SOUND},  // {x} {y} {z} {soundId}
    {COMMAND_REPORT_MISSION_AUDIO_EVENT_AT_CHAR, true, {eSyncedParamType::PED}},  // [Char] {soundId}
    {COMMAND_REPORT_MISSION_AUDIO_EVENT_AT_CAR, true, {eSyncedParamType::VEHICLE}},  // [Car] {soundId}
    {COMMAND_SHUT_ALL_CHARS_UP},  // {state}
    {COMMAND_SHUT_CHAR_UP, true, {eSyncedParamType::PED}},  // [Char] {state}
    {COMMAND_SHUT_CHAR_UP_FOR_SCRIPTED_SPEECH, true, {eSyncedParamType::PED}},  // [Char] {state}
    {COMMAND_ENABLE_CHAR_SPEECH, true, {eSyncedParamType::PED}},  // [Char]
    {COMMAND_DISABLE_CHAR_SPEECH, true, {eSyncedParamType::PED}},  // [Char] {stopNow}
    {COMMAND_TASK_SAY, true, {eSyncedParamType::PED}},  // [Char] {phrase}

    // Scene actors
    {COMMAND_TASK_STAY_IN_SAME_PLACE, true, {eSyncedParamType::PED}},  // [Char] {state}
    {COMMAND_TASK_PAUSE, true, {eSyncedParamType::PED}},  // [Char] {time}
    {COMMAND_TASK_TOGGLE_DUCK, true, {eSyncedParamType::PED}},  // [Char] {state}
    {COMMAND_TASK_TURN_CHAR_TO_FACE_COORD, true, {eSyncedParamType::PED}},  // [Char] {x} {y} {z}
    {COMMAND_TASK_HANDS_UP, true, {eSyncedParamType::PED}},  // [Char] {time}
    {COMMAND_TASK_DUCK, true, {eSyncedParamType::PED}},  // [Char] {time}
    {COMMAND_TASK_SCRATCH_HEAD, true, {eSyncedParamType::PED}},  // [Char]
    {COMMAND_TASK_SHAKE_FIST, true, {eSyncedParamType::PED}},  // [Char]
    {COMMAND_TASK_COWER, true, {eSyncedParamType::PED}},  // [Char]
    {COMMAND_TASK_SIT_DOWN, true, {eSyncedParamType::PED}},  // [Char] {time}
    {COMMAND_TASK_FALL_AND_GET_UP, true, {eSyncedParamType::PED}},  // [Char] {fallDown} {timeOnGround}
    {COMMAND_TASK_USE_MOBILE_PHONE, true, {eSyncedParamType::PED}},  // [Char] {start}
    {COMMAND_TASK_FOLLOW_PATH_NODES_TO_COORD, true, {eSyncedParamType::PED}},  // [Char] {x} {y} {z} {speed} {time}
    {COMMAND_TASK_GOTO_CHAR, true, {eSyncedParamType::PED, eSyncedParamType::PED}},  // [Char] [Char] {time} {radius}
    {COMMAND_TASK_GREET_PARTNER, true, {eSyncedParamType::PED, eSyncedParamType::PED}},  // [Char] [Char] {ratio} {style}
    {COMMAND_TASK_CHAR_SLIDE_TO_COORD, true, {eSyncedParamType::PED}},  // [Char] {x} {y} {z} {angle} {radius}
    {COMMAND_TASK_AIM_GUN_AT_COORD, true, {eSyncedParamType::PED}},  // [Char] {x} {y} {z} {time}
    {COMMAND_TASK_AIM_GUN_AT_CHAR, true, {eSyncedParamType::PED, eSyncedParamType::PED}},  // [Char] [Char] {time}
    {COMMAND_TASK_SHUFFLE_TO_NEXT_CAR_SEAT, true, {eSyncedParamType::PED, eSyncedParamType::VEHICLE}},  // [Char] [Car]
    {COMMAND_TASK_LEAVE_CAR_IMMEDIATELY, true, {eSyncedParamType::PED, eSyncedParamType::VEHICLE}},  // [Char] [Car]
    {COMMAND_TASK_EVERYONE_LEAVE_CAR, true, {eSyncedParamType::VEHICLE}},  // [Car]
    {COMMAND_SET_CHAR_ANIM_SPEED, true, {eSyncedParamType::PED}},  // [Char] {animName} [string] {speed}
    {COMMAND_SET_CHAR_ANIM_CURRENT_TIME, true, {eSyncedParamType::PED}},  // [Char] {animName} [string] {time}
    {COMMAND_SET_CHAR_VISIBLE, true, {eSyncedParamType::PED}},  // [Char] {state}
    {COMMAND_SET_CHAR_BLEEDING, true, {eSyncedParamType::PED}},  // [Char] {state}
    {COMMAND_SET_CHAR_COORDINATES_NO_OFFSET, true, {eSyncedParamType::PED}},  // [Char] {x} {y} {z}
    {COMMAND_SET_CHAR_COORDINATES_DONT_WARP_GANG, true, {eSyncedParamType::PED}},  // [Char] {x} {y} {z}
    {COMMAND_FREEZE_CHAR_POSITION, true, {eSyncedParamType::PED}},  // [Char] {state}
    {COMMAND_ATTACH_CHAR_TO_CAR, true, {eSyncedParamType::PED, eSyncedParamType::VEHICLE}},  // [Char] [Car] {offsets} {heading} {range} {weapon}
    {COMMAND_DETACH_CHAR_FROM_CAR, true, {eSyncedParamType::PED}},  // [Char]

    // Scene vehicles
    {COMMAND_OPEN_CAR_DOOR, true, {eSyncedParamType::VEHICLE}},  // [Car] {door}
    {COMMAND_OPEN_CAR_DOOR_A_BIT, true, {eSyncedParamType::VEHICLE}},  // [Car] {door} {value}
    {COMMAND_CONTROL_CAR_DOOR, true, {eSyncedParamType::VEHICLE}},  // [Car] {door} {state} {angle}
    {COMMAND_CLOSE_ALL_CAR_DOORS, true, {eSyncedParamType::VEHICLE}},  // [Car]
    {COMMAND_POP_CAR_DOOR, true, {eSyncedParamType::VEHICLE}},  // [Car] {door} {visibility}
    {COMMAND_FIX_CAR_DOOR, true, {eSyncedParamType::VEHICLE}},  // [Car] {door}
    {COMMAND_POP_CAR_BOOT, true, {eSyncedParamType::VEHICLE}},  // [Car]
    {COMMAND_DAMAGE_CAR_DOOR, true, {eSyncedParamType::VEHICLE}},  // [Car] {door}
    {COMMAND_DAMAGE_CAR_PANEL, true, {eSyncedParamType::VEHICLE}},  // [Car] {panelId}
    {COMMAND_POP_CAR_PANEL, true, {eSyncedParamType::VEHICLE}},  // [Car] {panelId} {visibility}
    {COMMAND_BURST_CAR_TYRE, true, {eSyncedParamType::VEHICLE}},  // [Car] {tireId}
    {COMMAND_FIX_CAR_TYRE, true, {eSyncedParamType::VEHICLE}},  // [Car] {tireId}
    {COMMAND_FORCE_CAR_LIGHTS, true, {eSyncedParamType::VEHICLE}},  // [Car] {lightMode}
    {COMMAND_SET_CAR_LIGHTS_ON, true, {eSyncedParamType::VEHICLE}},  // [Car] {state}
    {COMMAND_SET_CAR_ENGINE_ON, true, {eSyncedParamType::VEHICLE}},  // [Car] {state}
    {COMMAND_SWITCH_CAR_SIREN, true, {eSyncedParamType::VEHICLE}},  // [Car] {state}
    {COMMAND_SET_CAR_VISIBLE, true, {eSyncedParamType::VEHICLE}},  // [Car] {state}
    {COMMAND_CHANGE_CAR_COLOUR, true, {eSyncedParamType::VEHICLE}},  // [Car] {color1} {color2}
    {COMMAND_GIVE_VEHICLE_PAINTJOB, true, {eSyncedParamType::VEHICLE}},  // [Car] {paintjobId}
    {COMMAND_FREEZE_CAR_POSITION, true, {eSyncedParamType::VEHICLE}},  // [Car] {state}
    {COMMAND_ATTACH_CAR_TO_CAR, true, {eSyncedParamType::VEHICLE, eSyncedParamType::VEHICLE}},  // [Car] [Car] {offset} {rotation}; HEIST4 only (IsOpcodeSyncable)
    {COMMAND_SET_VEHICLE_AREA_VISIBLE, true, {eSyncedParamType::VEHICLE}},  // [Car] {interiorId}
    {COMMAND_SET_HELI_BLADES_FULL_SPEED, true, {eSyncedParamType::VEHICLE}},  // [Heli]
    {COMMAND_SET_PLANE_UNDERCARRIAGE_UP, true, {eSyncedParamType::VEHICLE}},  // [Plane] {state}
    {COMMAND_EXPLODE_CAR_IN_CUTSCENE_SHAKE_AND_BITS, true, {eSyncedParamType::VEHICLE}},  // [Car] {shake} {effect} {sound}

    {COMMAND_REQUEST_MODEL}};

static uint8_t textLengthBuffer[NUM_SYNCED_PARAMS];
static char textParamBuffer[NUM_SYNCED_PARAMS][256];

static uint16_t scriptParamCount = 0;
static uint16_t textParamCount = 0;

static uint32_t lastOpCodeProcessed;
static CRunningScript* lastProcessedScript;
// always-on trace of the cutscene flow (CoopAndreas_cutscene.log next to gta_sa.exe, at most 400 lines): what this client
// received and how the scene state changes, to find out why a guest does not see a scene (2026-10-20)
static void CutLog(const char* format, ...)
{
    static unsigned int lines = 0;
    if (lines >= 400)
        return;

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
        return;
    char* fileName = strrchr(path, '\\');
    if (!fileName)
        return;
    strcpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), "CoopAndreas_cutscene.log");

    FILE* file = fopen(path, "a");
    if (!file)
        return;

    char text[300];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    CPlayerPed* player = FindPlayerPed(-1);
    fprintf(file, "t=%u frame=%u host=%d auth=%d %s | pos=%.0f,%.0f,%.0f wide=%d\n", GetTickCount(), CTimer::m_FrameCounter,
        CLocalPlayer::m_bIsHost, CCoopMission::IsScriptAuthority(), text, player ? player->GetPosition().x : 0.0f,
        player ? player->GetPosition().y : 0.0f, player ? player->GetPosition().z : 0.0f, TheCamera.m_bWideScreenOn);
    fclose(file);
    ++lines;
}

// set by FinishedOpcodeProcessing_Hook around a command's handler and BuildAndSendOpcode
static CRunningScript* scriptRunningCommand;

CRunningScript* COpCodeSync::GetScriptRunningCommand()
{
    return scriptRunningCommand;
}

static uint8_t currentStringIdx = 0;

static uint16_t argCount = 0;
void __fastcall CRunningScript__CollectParameters_Hook_SwitchParametersContext(
    CRunningScript* script, SKIP_EDX, uint16_t count)
{
    for (uint8_t i = argCount; i < count + argCount; i++)
    {
        ScriptParams[i - argCount] = COpCodeSync::scriptParamsBuffer[i].value;
    }
    argCount += count;
    /*memcpy(ScriptParams, scriptParamsBuffer, sizeof scriptParamsBuffer);*/
}

void __fastcall CRunningScript__ReadTextLabelFromScript_Hook_SwitchParametersContext(
    CRunningScript* script, SKIP_EDX, char* ptr, uint8_t len)
{
    memset(ptr, 0, len);
    if (currentStringIdx >= NUM_SYNCED_PARAMS)
        return;
    strncpy(ptr, textParamBuffer[currentStringIdx], textLengthBuffer[currentStringIdx]);
    currentStringIdx++;
}

// texts a phone call may print (see `IsLocalPlayerOnPhone`), they are forwarded even if the script is not synced
static bool IsPhoneTextOpcode(int opcode)
{
    switch (opcode)
    {
        case COMMAND_PRINT:
        case COMMAND_PRINT_NOW:
        case COMMAND_PRINT_WITH_NUMBER_NOW:
        case COMMAND_PRINT_STRING_IN_STRING_NOW:
        case 0x00BA:  // print_big
        case 0x00BE:  // clear_prints
        case 0x054C:  // load_mission_text
            return true;
        default:
            return false;
    }
}

/// <param name="opcodeIdx">syncedOpcodes index</param>
bool COpCodeSync::IsOpcodeSyncable(int opcode, int* opcodeIdx, bool ignoreOpCodeSync, bool* pbMirrorOnly)
{
    if (opcode == 0x0917 && !CConfigLoader::ms_bMissionAudioZones) return false;
    // D: only the bike loading of Cop Wheels replays the attach, so a guest riding that bike sees it on the truck
    if (opcode == COMMAND_ATTACH_CAR_TO_CAR &&
        (!CConfigLoader::ms_bCoopMissionMachines || !lastProcessedScript ||
         _strnicmp(lastProcessedScript->m_szName, "HEIST4", 8) != 0))
        return false;
    // the end of Los Santos happens to every player of the coop Green Sabre (their own story moves to Badlands too)
    if (opcode == COMMAND_SET_INT_STAT &&
        (!lastProcessedScript || _strnicmp(lastProcessedScript->m_szName, "LA1FIN2", 8) != 0))
        return false;
    if (CActivityChallenge::Independent(lastProcessedScript)) return false;
    if (CCoopPool::Enabled() && CCoopPool::IsPool(lastProcessedScript)) return false;
    bool bScriptSynced = false;

    if (lastProcessedScript)
    {
        for (size_t i = 0; i < ms_iFreeSyncedScript; ++i)
        {
            if (strnicmp(ms_aszSyncedScripts[i], lastProcessedScript->m_szName, 7) == 0)
            {
                bScriptSynced = true;
                break;
            }
        }
    }

    // the story calls (Sweet, Smoke, Ryder...) are made by the MOB_* scripts of each player's own main.scm: every player gets
    // his own call and nobody hears the others' (their texts and voice are not forwarded)
    const bool bGeneral = ((CCoopMission::IsScriptAuthority() && ms_bSyncingEnabled) && bScriptSynced) || ignoreOpCodeSync ||
        CTaskSequenceSync::IsOpCodeTaskSynced((eScriptCommands)opcode);
    // the mission mirror layer adds the texts of the authority's mission family and the mission titles
    const bool bMirror = !bGeneral && CMissionMirror::WantsCommand(lastProcessedScript, static_cast<uint16_t>(opcode));
    if (pbMirrorOnly)
    {
        *pbMirrorOnly = bMirror;
    }

    if (bGeneral || bMirror)
    {
        for (int i = 0; i < ARRAY_SIZE(syncedOpcodes); ++i)
        {
            if (syncedOpcodes[i].m_wOpCode == opcode)
            {
                if (opcodeIdx)
                {
                    *opcodeIdx = i;
                }

                return true;
            }
        }
    }

    return false;
}

// T97: CAudioZones::SwitchAudioZone (gta-reversed AudioZones.cpp, 0x508320; the 0917 handler at 0x476810 calls it after
// ReadTextLabelFromScript(8) and CollectParameters(1)). Zones are off until a mission switches them on.
static char networkAudioZones[8][9];
static int networkAudioZoneCount = 0;
static void NoteNetworkAudioZone(const char* name, bool on)
{
    char key[9]{};
    strncpy_s(key, name, 8);
    for (int i = 0; i < networkAudioZoneCount; ++i)
    {
        if (_stricmp(networkAudioZones[i], key) != 0) continue;
        if (!on) memmove(networkAudioZones[i], networkAudioZones[i + 1], sizeof(networkAudioZones[0]) * (--networkAudioZoneCount - i));
        return;
    }
    if (on && networkAudioZoneCount < 8) memcpy(networkAudioZones[networkAudioZoneCount++], key, sizeof(key));
}

void COpCodeSync::ResetNetworkAudioZones()
{
    for (int i = 0; i < networkAudioZoneCount; ++i)
    {
        plugin::Call<0x508320, const char*, bool>(networkAudioZones[i], false);
        CMissionMirror::LogEventf("audio-zone-reset name=%.8s", networkAudioZones[i]);
    }
    networkAudioZoneCount = 0;
}

bool COpCodeSync::IsLocalMissionRunning()
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission)
            return true;
    }

    return false;
}

bool COpCodeSync::ShouldShowNetworkText(const char* key, int kind, int source)
{
    struct SShownText
    {
        DWORD tick;
        int source;
    };
    static std::unordered_map<std::string, SShownText> lastShown;

    std::string id = std::to_string(kind) + ":" + std::string(key, strnlen(key, 8));
    DWORD now = GetTickCount();

    // the duplicate of a text already shown by the other path: drop it (and keep the original entry)
    auto it = lastShown.find(id);
    if (it != lastShown.end() && it->second.source != source && now - it->second.tick < 2000)
        return false;

    if (lastShown.size() > 256)
        lastShown.clear();

    lastShown[id] = SShownText{now, source};
    return true;
}

// key -> name of the mission that sends it per player (a new mission has another name, so old keys stop counting)
static std::unordered_map<std::string, std::string> perPlayerTexts;

static std::string TextKeyOf(const char* key)
{
    std::string id(key, strnlen(key, 8));
    for (char& c : id)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return id;
}

void COpCodeSync::MarkPerPlayerText(CRunningScript* script, const char* key)
{
    if (script == nullptr || !script->m_bIsMission)
        return;

    if (perPlayerTexts.size() > 256)
        perPlayerTexts.clear();

    perPlayerTexts[TextKeyOf(key)] = std::string(script->m_szName, strnlen(script->m_szName, 8));
}

static bool IsPerPlayerText(CRunningScript* script, const char* key)
{
    if (script == nullptr || !script->m_bIsMission)
        return false;

    auto it = perPlayerTexts.find(TextKeyOf(key));
    return it != perPlayerTexts.end() && it->second == std::string(script->m_szName, strnlen(script->m_szName, 8));
}

static bool IsValidGlobalVarOffset(unsigned int offset)
{
    return offset != 0 && offset <= 200000 - sizeof(int);
}

void COpCodeSync::ProcessOnscreenTimerSync()
{
    static bool lastSentAnything = false;

    bool hostMission = CCoopMission::IsScriptAuthority() && IsSyncedMissionRunning();

    Packets::Scripts::OnscreenTimerSync packet{};
    bool anything = false;

    COnscreenTimerEntry& clock = CUserDisplay::OnscnTimer.m_Clock;
    if (hostMission && IsValidGlobalVarOffset(clock.m_nVarId))
    {
        packet.enabled = true;
        packet.varOffset = clock.m_nVarId;
        strncpy_s(packet.textKey, clock.m_szDescriptionTextKey, sizeof(packet.textKey) - 1);
        packet.direction = clock.m_nTimerDirection ? 1 : 0;
        packet.value = *(int*)&CTheScripts::ScriptSpace[clock.m_nVarId];
        packet.paused = CUserDisplay::OnscnTimer.m_bPaused;
        anything = true;
    }

    for (int i = 0; i < 4; i++)
    {
        COnscreenCounterEntry& counter = CUserDisplay::OnscnTimer.m_aCounters[i];
        if (!hostMission || !IsValidGlobalVarOffset(counter.m_nVarId) || counter.m_nType > 2)
            continue;

        auto& state = packet.counters[i];
        state.enabled = true;
        state.varOffset = counter.m_nVarId;
        strncpy_s(state.textKey, counter.m_szDescriptionTextKey, sizeof(state.textKey) - 1);
        state.type = (uint8_t)counter.m_nType;
        state.colour = counter.m_nColourId;
        state.value = *(int*)&CTheScripts::ScriptSpace[counter.m_nVarId];
        anything = true;
    }

    // one last packet with everything disabled clears the copies
    if (!anything && !lastSentAnything)
        return;

    GetPacketFactory().Send(packet);
    lastSentAnything = anything;
}

bool COpCodeSync::IsScriptSynced(CRunningScript* script)
{
    if (CActivityChallenge::Independent(script)) return false;
    if (CCoopPool::Enabled() && CCoopPool::IsPool(script)) return false;
    if (script && _strnicmp(script->m_szName, "SWEET6", 8) == 0 &&
        (!CConfigLoader::ms_bCoopLowrider || !CConfigLoader::ms_bVanillaScripts || CMissionMode::IsPlayingAlone()))
        return false;
    if (!script)
        return false;

    for (size_t i = 0; i < ms_iFreeSyncedScript; ++i)
    {
        if (strnicmp(ms_aszSyncedScripts[i], script->m_szName, 7) == 0)
            return true;
    }

    return false;
}

bool COpCodeSync::IsSyncedMissionRunning()
{
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        if (script->m_bIsMission && IsScriptSynced(script)) return true;
    return false;
}

static bool TaskTreeHasPhoneTask(CTask* task, int depth)
{
    if (!task || depth > 8)
        return false;

    const eTaskType taskType = task->GetTaskType();
    if (taskType == TASK_COMPLEX_USE_MOBILE_PHONE || taskType == TASK_SIMPLE_PHONE_TALK)
        return true;

    return TaskTreeHasPhoneTask(task->GetSubTask(), depth + 1);
}

bool COpCodeSync::IsLocalPlayerOnPhone()
{
    CPlayerPed* playerPed = FindPlayerPed(0);
    if (!playerPed || !playerPed->m_pIntelligence)
        return false;

    CTaskManager& taskManager = playerPed->m_pIntelligence->m_TaskMgr;

    for (int i = 0; i < TASK_PRIMARY_MAX; i++)
    {
        if (TaskTreeHasPhoneTask(taskManager.m_aPrimaryTasks[i], 0))
            return true;
    }

    for (int i = 0; i < TASK_SECONDARY_MAX; i++)
    {
        if (TaskTreeHasPhoneTask(taskManager.m_aSecondaryTasks[i], 0))
            return true;
    }

    return false;
}

// missions that run individually on every client (their actors are local, never synced)
bool COpCodeSync::IsLocalOnlyMissionRunning()
{
    static const char* localOnlyMissions[] = {"SWEET6", "LOWR"};

    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (!script->m_bIsMission || IsScriptSynced(script))
            continue;

        for (const char* missionName : localOnlyMissions)
        {
            if (strnicmp(missionName, script->m_szName, 7) == 0)
                return true;
        }
    }

    return false;
}

// see `OpcodeParameter` for the format
static bool IsEntityParam(int idx, int paramIdx)
{
    return syncedOpcodes[idx].m_bHasComplexParams && paramIdx < NUM_ENTITY_PARAMS &&
           syncedOpcodes[idx].m_aParamTypes[paramIdx] != eSyncedParamType::NONE;
}

static int GetSerializedParamSize(const OpcodeParameter& param, bool isEntityParam)
{
    if (!isEntityParam)
    {
        return sizeof(int32_t);
    }

    switch (param.entityType)
    {
        case eSyncedParamType::PLAYER:
            return sizeof(uint8_t) + ids::PLAYERID_SIZE;
        case eSyncedParamType::PED:
        case eSyncedParamType::VEHICLE:
            return sizeof(uint8_t) + ids::ENTITYID_SIZE;
        default:
            return sizeof(uint8_t) + sizeof(int32_t);
    }
}

static uint8_t* WriteParam(uint8_t* pCurrent, const OpcodeParameter& param, bool isEntityParam)
{
    if (!isEntityParam)
    {
        ids::WriteU32(pCurrent, static_cast<uint32_t>(param.value));
        return pCurrent + sizeof(int32_t);
    }

    *pCurrent = static_cast<uint8_t>(param.entityType);
    pCurrent += sizeof(uint8_t);

    switch (param.entityType)
    {
        case eSyncedParamType::PLAYER:
            ids::WritePlayerId(pCurrent, param.playerid);
            return pCurrent + ids::PLAYERID_SIZE;
        case eSyncedParamType::PED:
            ids::WriteEntityId(pCurrent, param.pedid);
            return pCurrent + ids::ENTITYID_SIZE;
        case eSyncedParamType::VEHICLE:
            ids::WriteEntityId(pCurrent, param.vehicleid);
            return pCurrent + ids::ENTITYID_SIZE;
        default:
            ids::WriteU32(pCurrent, static_cast<uint32_t>(param.value));
            return pCurrent + sizeof(int32_t);
    }
}

// returns nullptr if the data is malformed
static const uint8_t* ReadParam(
    const uint8_t* pCurrent, const uint8_t* pEnd, OpcodeParameter& param, bool isEntityParam)
{
    param = OpcodeParameter();

    if (!isEntityParam)
    {
        if (pEnd - pCurrent < (int)sizeof(int32_t))
            return nullptr;

        param.value = static_cast<int>(ids::ReadU32(pCurrent));
        return pCurrent + sizeof(int32_t);
    }

    if (pEnd - pCurrent < (int)sizeof(uint8_t))
        return nullptr;

    eSyncedParamType entityType = static_cast<eSyncedParamType>(*pCurrent);
    pCurrent += sizeof(uint8_t);

    param.value = -1;
    param.entityType = entityType;

    switch (entityType)
    {
        case eSyncedParamType::PLAYER:
            if (pEnd - pCurrent < (int)ids::PLAYERID_SIZE)
                return nullptr;

            param.playerid = ids::ReadPlayerId(pCurrent);
            return pCurrent + ids::PLAYERID_SIZE;
        case eSyncedParamType::PED:
            if (pEnd - pCurrent < (int)ids::ENTITYID_SIZE)
                return nullptr;

            param.pedid = ids::ReadEntityId<pedid_t>(pCurrent);
            return pCurrent + ids::ENTITYID_SIZE;
        case eSyncedParamType::VEHICLE:
            if (pEnd - pCurrent < (int)ids::ENTITYID_SIZE)
                return nullptr;

            param.vehicleid = ids::ReadEntityId<vehicleid_t>(pCurrent);
            return pCurrent + ids::ENTITYID_SIZE;
        case eSyncedParamType::NONE:
            if (pEnd - pCurrent < (int)sizeof(int32_t))
                return nullptr;

            param.value = static_cast<int>(ids::ReadU32(pCurrent));
            return pCurrent + sizeof(int32_t);
        default:
            return nullptr;
    }
}

std::vector<uint8_t> COpCodeSync::SerializeOpcode(int idx, int& outSize)
{
    for (int i = 0; i < scriptParamCount; i++)
    {
        if (!IsEntityParam(idx, i))
        {
            continue;
        }

        OpcodeParameter& param = scriptParamsBuffer[i];

        // CChat::AddMessage("Parsing complex opcode %04x parameter %d...", lastOpCodeProcessed, i);
        switch (syncedOpcodes[idx].m_aParamTypes[i])
        {
            case eSyncedParamType::PED:
            {
                // CChat::AddMessage("Trying to parse a ped handle %d...", param.value);
                if (auto ped = CPools::GetPed(param.value))
                {
                    if (ped->m_nPedType > 3)  // is a regular ped
                    {
                        auto networkPed = CNetworkPedManager::GetPed(ped);
                        if (networkPed && networkPed->m_bSyncing)
                        {
                            param.entityType = eSyncedParamType::PED;
                            param.pedid = networkPed->m_pedId;
                            // CChat::AddMessage("Parsed ped id %s...", to_string(networkPed->m_pedId).c_str());
                        }
                        else
                            param.SetValue(-1);
                    }
                    else  // is a player ped
                    {
                        if (ped == FindPlayerPed(0))
                        {
                            param.entityType = eSyncedParamType::PLAYER;
                            param.playerid = CNetworkPlayerManager::m_localPlayerId;
                        }
                        else if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(ped))
                        {
                            param.entityType = eSyncedParamType::PLAYER;
                            param.playerid = networkPlayer->m_playerId;
                        }
                        else
                            param.SetValue(-1);
                    }
                }
                else
                    param.SetValue(-1);

                break;
            }
            case eSyncedParamType::PLAYER:
            {
                // CChat::AddMessage("Trying to parse a player...");
                if (!param.value)
                {
                    param.entityType = eSyncedParamType::PLAYER;
                    param.playerid = CNetworkPlayerManager::m_localPlayerId;
                    // CChat::AddMessage("Parsed player id %s (me)...", to_string(param.playerid).c_str());
                    break;
                }

                CPlayerPed* player = nullptr;
                if (param.value > 0 && param.value < Config::MAX_SERVER_PLAYERS + 2)
                {
                    player = CWorld::Players[param.value].m_pPed;
                }

                auto networkPlayer = player ? CNetworkPlayerManager::GetPlayer(player) : nullptr;
                if (networkPlayer)
                {
                    param.entityType = eSyncedParamType::PLAYER;
                    param.playerid = networkPlayer->m_playerId;
                    // CChat::AddMessage("Parsed player id %s...", to_string(param.playerid).c_str());
                }
                else
                    param.SetValue(-1);

                break;
            }
            case eSyncedParamType::VEHICLE:
            {
                // CChat::AddMessage("Trying to parse a vehicle...");
                if (auto vehicle = CPools::GetVehicle(param.value))
                {
                    if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle))
                    {
                        param.entityType = eSyncedParamType::VEHICLE;
                        param.vehicleid = networkVehicle->m_vehicleId;
                        // CChat::AddMessage("Parsed vehicle id %s...", to_string(param.vehicleid).c_str());
                    }
                    else
                        param.SetValue(-1);
                }
                else
                    param.SetValue(-1);

                break;
            }
        }
    }

    int textCount = (textParamCount < NUM_SYNCED_PARAMS) ? textParamCount : NUM_SYNCED_PARAMS;

    int dataSize = sizeof(OpcodeSyncHeader) + textCount * sizeof(uint8_t);

    for (int i = 0; i < scriptParamCount; i++)
    {
        dataSize += GetSerializedParamSize(scriptParamsBuffer[i], IsEntityParam(idx, i));
    }

    for (int i = 0; i < textCount; i++)
    {
        dataSize += textLengthBuffer[i];
    }

    std::vector<uint8_t> buffer(dataSize);
    uint8_t* pCurrent = buffer.data();

    OpcodeSyncHeader header;
    header.opcode = lastOpCodeProcessed;
    header.intParamCount = scriptParamCount;
    header.stringParamCount = textCount;

    memcpy(pCurrent, &header, sizeof(OpcodeSyncHeader));
    pCurrent += sizeof(OpcodeSyncHeader);

    for (int i = 0; i < scriptParamCount; i++)
    {
        pCurrent = WriteParam(pCurrent, scriptParamsBuffer[i], IsEntityParam(idx, i));
    }

    for (int i = 0; i < textCount; i++)
    {
        *pCurrent = static_cast<uint8_t>(textLengthBuffer[i]);
        pCurrent += sizeof(uint8_t);

        memcpy(pCurrent, textParamBuffer[i], textLengthBuffer[i]);
        pCurrent += textLengthBuffer[i];
    }

    COOP_VERIFY(dataSize <= Packets::Scripts::OpCodeSync::MAX_BUFFER_SIZE);

    outSize = dataSize;
    return buffer;
}

static void SkipDeathArrestTutorial(CRunningScript* script, uint16_t opcode)
{
    // the coop main.scm carries the same HELP bytes as the original one (checked below), so both script modes skip
    if (!script || COpCodeSync::bProcessingNetworkOpcode || _strnicmp(script->m_szName, "HELP", 8) != 0)
        return;

    // HELP uses PRINT_HELP_FOREVER (0512 + type 09 + eight-byte GXT key)
    // directly followed by WAIT (0001 + type 05 + little-endian 8000).
    constexpr uint8_t print[] = {0x12, 0x05, 0x09};
    constexpr uint8_t wait[] = {0x01, 0x00, 0x05, 0x40, 0x1F};
    constexpr size_t printSize = sizeof(print) + 8;
    const bool isWait = opcode == COMMAND_WAIT;
    if (!isWait && opcode != COMMAND_PRINT_HELP_FOREVER)
        return;
    const size_t size = printSize + (isWait ? sizeof(wait) : 0);
    const uintptr_t base = reinterpret_cast<uintptr_t>(CTheScripts::ScriptSpace);
    const uintptr_t ip = reinterpret_cast<uintptr_t>(script->m_pCurrentIP);
    if (ip < base + size || ip > base + 200000)
        return;
    const auto* instruction = reinterpret_cast<const uint8_t*>(ip - size);
    if (memcmp(instruction, print, sizeof(print)) != 0 ||
        (isWait && memcmp(instruction + printSize, wait, sizeof(wait)) != 0))
        return;
    const auto* key = instruction + sizeof(print);
    if (key[6] || key[7] || key[5] < '1' ||
        !((memcmp(key, "HOSP_", 5) == 0 && key[5] <= '3') ||
          (memcmp(key, "BUST_", 5) == 0 && key[5] <= '4')))
        return;
    // one line per text, also with the option off: shows whether a player who saw the tutorial had it checked
    if (!isWait)
        CMissionMirror::LogEventf("death-help key=%.6s skip=%d vanilla=%d", key, CConfigLoader::ms_bSkipDeathArrestHelp ? 1 : 0,
            CConfigLoader::ms_bVanillaScripts ? 1 : 0);
    if (!CConfigLoader::ms_bSkipDeathArrestHelp)
        return;
    if (isWait)
        script->m_nWakeTime = 0;
    else
        CHud::SetHelpMessage(nullptr, true, false, false);
}

void BuildAndSendOpcode()
{
    SkipDeathArrestTutorial(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed));
    CActivityChallenge::AfterCommand(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed) & 0x7FFF);
    CCoopPool::AfterCommand(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed));
    // the mission mirror layer's post-command call, after every command of every script
    CMissionMirror::OnCommandExecuted(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed));
    CCameraMirror::OnCommandExecuted(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed));
    CPatientHandover::OnCommandExecuted(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed));

    if (!CTaskSequenceSync::OnOpCodeExecuted((eScriptCommands)lastOpCodeProcessed))
    {
        memset(textParamBuffer, 0, sizeof textParamBuffer);
        memset(textLengthBuffer, 0, sizeof textLengthBuffer);
        scriptParamCount = 0;
        textParamCount = 0;
        return;
    }

    switch (lastOpCodeProcessed)
    {
        case 0xD8:
        case 0x7FB:
        case 0x864:
        case 0x8E7:
        case 0x98E:
        case 0x9B4:
        case 0x9E6:
            CEntryExitMarkerSync::ms_bUpdateAfterProcessingScripts = true;
            break;
    }

    int idx = 0;
    bool bMirrorOnly = false;
    if (!COpCodeSync::IsOpcodeSyncable(lastOpCodeProcessed, &idx, false, &bMirrorOnly))
    {
        // every other early-return clears the pending params; without this the counters leak into the
        // following commands when the state changed since the collect (textParamCount reached 18516 in a
        // long 2026-11-07 session and strncpy_s wrote past textParamBuffer, crashing the game)
        memset(textParamBuffer, 0, sizeof textParamBuffer);
        memset(textLengthBuffer, 0, sizeof textLengthBuffer);
        scriptParamCount = 0;
        textParamCount = 0;
        return;
    }

    // the mission already sends this text to each player for his own state: the host's copy would cover the guest's
    // dialogue subtitles with the host's objective (PRINT_NOW replaces the shown text, CMessages::AddMessage2 0x69E360)
    if (textParamCount >= 1 && IsPerPlayerText(lastProcessedScript, textParamBuffer[0]))
    {
        switch (lastOpCodeProcessed)
        {
            case COMMAND_PRINT:
            case COMMAND_PRINT_NOW:
            case COMMAND_PRINT_WITH_NUMBER_NOW:
            case COMMAND_PRINT_STRING_IN_STRING_NOW:
            case COMMAND_CLEAR_THIS_PRINT:
            case 0x00BA:  // print_big
            case 0x03D6:  // clear_this_big_print
            case 0x03E5:  // print_help
            {
                static DWORD lastLog = 0;
                if (GetTickCount() - lastLog > 5000)
                {
                    lastLog = GetTickCount();
                    CMissionMirror::LogEventf("tx-text op=%04X key=%.8s dropped=per-player", lastOpCodeProcessed,
                        textParamBuffer[0]);
                }
                memset(textParamBuffer, 0, sizeof textParamBuffer);
                memset(textLengthBuffer, 0, sizeof textLengthBuffer);
                scriptParamCount = 0;
                textParamCount = 0;
                return;
            }
        }
    }

    int dataSize = 0;
    std::vector<uint8_t> buffer = COpCodeSync::SerializeOpcode(idx, dataSize);

    // the mission mirror layer keeps back what doesn't go out now (a print_big that isn't a mission title) and notes the
    // texts a joiner gets
    if (!CMissionMirror::OnCommandSerialized(
            lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed), bMirrorOnly, buffer.data(), dataSize))
    {
        memset(textParamBuffer, 0, sizeof textParamBuffer);
        memset(textLengthBuffer, 0, sizeof textLengthBuffer);
        scriptParamCount = 0;
        textParamCount = 0;
        return;
    }

    // missions print the same text every frame (countdowns, "get back in the car"...); only send it again when it
    // changes or after a second (the receiver keeps short texts on screen longer, see HandlePacket)
    static uint32_t textsSent = 0;
    switch (lastOpCodeProcessed)
    {
        case COMMAND_CLEAR_THIS_PRINT:
        {
            // INTRO2 clears its texts every frame while on the bike: only send a clear when a text went out since the
            // last one for this key (thousands per second dropped the guest's fps)
            static std::map<std::vector<uint8_t>, uint32_t> lastClears;
            auto it = lastClears.find(buffer);
            if (it != lastClears.end() && it->second == textsSent)
            {
                memset(textParamBuffer, 0, sizeof textParamBuffer);
                memset(textLengthBuffer, 0, sizeof textLengthBuffer);
                scriptParamCount = 0;
                textParamCount = 0;
                return;
            }
            lastClears[buffer] = textsSent;
            break;
        }
        case COMMAND_PRINT:
        case COMMAND_PRINT_NOW:
        case COMMAND_PRINT_WITH_NUMBER_NOW:
        case COMMAND_PRINT_STRING_IN_STRING_NOW:
        case 0x00BA:  // print_big
        case 0x01E3:  // print_with_number_big
        case 0x03E5:  // print_help
        {
            static std::unordered_map<uint16_t, std::pair<std::vector<uint8_t>, DWORD>> lastTexts;
            DWORD now = GetTickCount();
            auto& last = lastTexts[lastOpCodeProcessed];
            if (buffer == last.first && now - last.second < 1000)
            {
                memset(textParamBuffer, 0, sizeof textParamBuffer);
                memset(textLengthBuffer, 0, sizeof textLengthBuffer);
                scriptParamCount = 0;
                textParamCount = 0;
                return;
            }
            last.first = buffer;
            last.second = now;
            textsSent++;
            break;
        }
    }

    // TODO(v0.3.1-alpha): serialize directly into the packet structure avoiding std::vector creation
    static Packets::Scripts::OpCodeSync packet{};
    packet.size = dataSize;
    memcpy(packet.buffer, buffer.data(), dataSize);
    GetPacketFactory().Send(packet);

    memset(textParamBuffer, 0, sizeof textParamBuffer);
    memset(textLengthBuffer, 0, sizeof textLengthBuffer);
    scriptParamCount = 0;
    textParamCount = 0;
}

bool IsOpcodeRequiresStrictCheck(uint16_t opcode)
{
    if (opcode == COMMAND_TASK_DRIVE_BY || COpCodeSync::ms_bProcessingTaskSequence)
    {
        return false;
    }
    return true;
}

// The cars this client drove lately (newest first), for the replayed clear_area protection in HandlePacket.
static constexpr int RECENT_CARS = 3;
static int recentCarRefs[RECENT_CARS] = {-1, -1, -1};
static void TrackRecentCars()
{
    CPlayerPed* player = FindPlayerPed(0);
    if (!player || !player->m_nPedFlags.bInVehicle)
        return;
    CVehicle* car = player->m_pVehicle;
    if (!car || !CPools::ms_pVehiclePool->IsObjectValid(car) || !car->IsVTableValid())
        return;
    const int ref = CPools::GetVehicleRef(car);
    if (recentCarRefs[0] == ref)
        return;
    int from = RECENT_CARS - 1;
    for (int i = 0; i < RECENT_CARS; ++i)
        if (recentCarRefs[i] == ref)
            from = i;
    for (int i = from; i > 0; --i)
        recentCarRefs[i] = recentCarRefs[i - 1];
    recentCarRefs[0] = ref;
}

void COpCodeSync::HandlePacket(const uint8_t* buffer, int bufferSize)
{
    if (CMissionLock::IsLocalIntroRunning())
        return;

    if (!buffer || bufferSize < sizeof(OpcodeSyncHeader))
    {
        return;
    }

    const uint8_t* current = buffer;

    OpcodeSyncHeader header;
    memcpy(&header, current, sizeof(header));
    current += sizeof(header);

    if (CCoopMission::IsScriptAuthority() && !CTaskSequenceSync::IsOpCodeTaskSynced((eScriptCommands)header.opcode))
    {
        return;
    }

    // a phone call text must not overwrite the texts of a mission the receiver is playing on its own; during a synced
    // mission every client runs its own copy of the same script, so those texts still go through
    if (IsPhoneTextOpcode(header.opcode) && IsLocalMissionRunning() && !IsSyncedMissionRunning())
    {
        return;
    }

    for (int i = 0; i < NUM_SYNCED_PARAMS; i++)
    {
        scriptParamsBuffer[i] = OpcodeParameter();
    }
    memset(textLengthBuffer, 0, sizeof(textLengthBuffer));
    memset(textParamBuffer, 0, sizeof(textParamBuffer));
    scriptParamCount = 0;
    textParamCount = 0;
    currentStringIdx = 0;

    scriptParamCount = header.intParamCount;
    if (scriptParamCount > NUM_SYNCED_PARAMS)
        scriptParamCount = NUM_SYNCED_PARAMS;

    textParamCount = header.stringParamCount;
    if (textParamCount > NUM_SYNCED_PARAMS)
        textParamCount = NUM_SYNCED_PARAMS;

    {
        // reject opcodes that are not in the synced whitelist even when they carry no int params
        bool opcodeKnown = false;
        for (int knownIdx = 0; knownIdx < ARRAY_SIZE(syncedOpcodes); knownIdx++)
        {
            if (syncedOpcodes[knownIdx].m_wOpCode == header.opcode)
            {
                opcodeKnown = true;
                break;
            }
        }
        if (!opcodeKnown)
            return;
    }

    if (scriptParamCount)
    {
        int idx = 0;
        bool found = false;
        for (; idx < ARRAY_SIZE(syncedOpcodes); idx++)
        {
            if (syncedOpcodes[idx].m_wOpCode == header.opcode)
            {
                found = true;
                break;
            }
        }

        if (!found)
            return;

        for (int i = 0; i < scriptParamCount; i++)
        {
            current = ReadParam(current, buffer + bufferSize, scriptParamsBuffer[i], IsEntityParam(idx, i));
            if (current == nullptr)
            {
                return;
            }
        }

        if (syncedOpcodes[idx].m_bHasComplexParams)
        {
            /*if (header.opcode == 0x0605)
                CChat::AddMessage("Parsing complex opcode %04x...", header.opcode);*/

            int max = VMIN(scriptParamCount, NUM_ENTITY_PARAMS);
            for (int i = 0; i < max; i++)
            {
                OpcodeParameter& param = scriptParamsBuffer[i];

                switch (syncedOpcodes[idx].m_aParamTypes[i])
                {
                    case eSyncedParamType::PED:
                    {
                        /*if (header.opcode == 0x0605)
                            CChat::AddMessage("Parsing ped...");*/
                        if (COpCodeSync::ms_bProcessingTaskSequence && param.entityType == eSyncedParamType::NONE &&
                            param.value == -1)
                        {
                            break;
                        }

                        if (param.entityType == eSyncedParamType::PED)
                        {
                            /*if (header.opcode == 0x0605)
                                CChat::AddMessage("Trying to parse as a regular ped...");*/
                            if (auto networkPed = CNetworkPedManager::GetPed(param.pedid))
                            {
                                /*if (header.opcode == 0x0605)
                                    CChat::AddMessage("Network instance found...");*/
                                if (auto ped = networkPed->m_pPed)
                                {
                                    if (!ped->IsVTableValid())
                                        return;

                                    param.value = CPools::GetPedRef(ped);
                                    /*if (header.opcode == 0x0605)
                                        CChat::AddMessage("Parsed ped id %s...",
                                       to_string(networkPed->m_pedId).c_str());*/
                                }
                                else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                                {
                                    param.value = -1;
                                }
                                else
                                    return;
                            }
                            else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                            {
                                param.value = -1;
                            }
                            else
                                return;
                        }
                        else
                        {
                            if (param.entityType == eSyncedParamType::PLAYER &&
                                param.playerid == CNetworkPlayerManager::m_localPlayerId)
                            {
                                if (auto playerPed = FindPlayerPed(0))
                                {
                                    param.value = CPools::GetPedRef(playerPed);
                                }
                            }
                            else
                            {
                                /*if (header.opcode == 0x0605)
                                    CChat::AddMessage("Trying to parse as a player ped... %s",
                                   to_string(param.playerid).c_str());*/
                                if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(param.playerid))
                                {
                                    /*if (header.opcode == 0x0605)
                                        CChat::AddMessage("Network instance found...");*/
                                    if (auto ped = networkPlayer->m_pPed)
                                    {
                                        if (!ped->IsVTableValid())
                                            return;

                                        param.value = CPools::GetPedRef(ped);
                                        /*if (header.opcode == 0x0605)
                                            CChat::AddMessage("Parsed player id as ped %s...",
                                           to_string(networkPlayer->m_playerId).c_str());*/
                                    }
                                    else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                                    {
                                        param.value = -1;
                                    }
                                    else
                                        return;
                                }
                                else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                                {
                                    param.value = -1;
                                }
                                else
                                    return;
                            }
                        }

                        break;
                    }
                    case eSyncedParamType::PLAYER:
                    {
                        /*if (param.playerid == CNetworkPlayerManager::m_localPlayerId)
                        {*/
                        param.value = 0;
                        /*if (header.opcode == 0x0605)
                            CChat::AddMessage("Parsed player id %s (me)...",
                           to_string(CNetworkPlayerManager::m_localPlayerId).c_str());*/
                        /*    break;
                        }*/

                        // if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(param.playerid))
                        //{
                        //     if (networkPlayer->m_pPed)
                        //     {
                        //         int playerNum = networkPlayer->GetInternalId();
                        //         if (playerNum)
                        //         {
                        //             param.value = playerNum;
                        //             /*if (header.opcode == 0x0605)
                        //                 CChat::AddMessage("Parsed player id %s...",
                        //                 to_string(networkPlayer->m_playerId).c_str());*/
                        //         }
                        //         else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                        //         {
                        //             param.value = -1;
                        //         }
                        //         else return;
                        //     }
                        //     else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                        //     {
                        //         param.value = -1;
                        //     }
                        //     else return;
                        // }
                        // else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                        //{
                        //     param.value = -1;
                        // }
                        // else return;

                        break;
                    }
                    case eSyncedParamType::VEHICLE:
                    {
                        // NONE (unresolved on the sender's side) has an invalid id, so it's never found
                        if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(param.vehicleid))
                        {
                            if (auto vehicle = networkVehicle->m_pVehicle)
                            {
                                if (!vehicle->IsVTableValid())
                                    return;

                                param.value = CPools::GetVehicleRef(vehicle);
                                /*if (header.opcode == 0x0605)
                                    CChat::AddMessage("Parsed vehicle id %s...",
                                   to_string(networkVehicle->m_vehicleId).c_str());*/
                            }
                            else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                            {
                                param.value = -1;
                            }
                            else
                                return;
                        }
                        else if (!IsOpcodeRequiresStrictCheck(header.opcode))
                        {
                            param.value = -1;
                        }
                        else
                            return;

                        break;
                    }
                    default:
                        break;
                }
            }
            /*if (header.opcode == 0x0605)
                CChat::AddMessage("OK");*/
        }
    }

    for (int i = 0; i < textParamCount; i++)
    {
        if (current >= buffer + bufferSize)
        {
            return;
        }

        textLengthBuffer[i] = *current;
        current += sizeof(uint8_t);

        if (current + textLengthBuffer[i] > buffer + bufferSize || textLengthBuffer[i] > 255)
        {
            return;
        }

        memcpy(textParamBuffer[i], current, textLengthBuffer[i]);
        textParamBuffer[i][textLengthBuffer[i]] = '\0';
        current += textLengthBuffer[i];
    }

    // the sender only resends a repeated text after a second, keep short ones on screen until the next one
    if (header.opcode == COMMAND_PRINT_WITH_NUMBER_NOW && scriptParamCount >= 2 && scriptParamsBuffer[1].value < 1500)
    {
        scriptParamsBuffer[1].value = 1500;
    }
    // (COMMAND_PRINT too: Drive-By reprints SWE4_12/SWE4_03 for 100 ms every frame at the end)
    if ((header.opcode == COMMAND_PRINT_NOW || header.opcode == COMMAND_PRINT) && scriptParamCount >= 1 && scriptParamsBuffer[0].value < 1500)
    {
        scriptParamsBuffer[0].value = 1500;
    }

    // the same mission may also send this text with Coop.Print*ForNetworkPlayer
    if (textParamCount >= 1)
    {
        int kind = -1;
        switch (header.opcode)
        {
            case COMMAND_PRINT:
            case COMMAND_PRINT_NOW:
                kind = 0;
                break;
            case 0x00BA:  // print_big
                kind = 1;
                break;
            case 0x03E5:  // print_help
                kind = 2;
                break;
        }
        if (kind != -1 && !COpCodeSync::ShouldShowNetworkText(textParamBuffer[0], kind, NETWORK_TEXT_FROM_OPCODE))
        {
            CMissionMirror::LogEventf("rx-text op=%04X key=%.8s dropped=dup", header.opcode, textParamBuffer[0]);
            return;
        }
        if (kind != -1 || header.opcode == COMMAND_CLEAR_THIS_PRINT)
        {
            CMissionMirror::LogEventf("rx-text op=%04X key=%.8s time=%d", header.opcode, textParamBuffer[0],
                scriptParamCount >= 1 ? scriptParamsBuffer[0].value : -1);
        }
    }

    // T97: the host's mission music zone; remembered so a mission that ends without its switch-off leaves no music here
    if (header.opcode == 0x0917)
    {
        if (!CConfigLoader::ms_bMissionAudioZones || textParamCount < 1 || scriptParamCount < 1)
            return;
        NoteNetworkAudioZone(textParamBuffer[0], scriptParamsBuffer[0].value != 0);
        CMissionMirror::LogEventf("rx-audio-zone name=%.8s on=%d", textParamBuffer[0], scriptParamsBuffer[0].value != 0 ? 1 : 0);
    }

    static CRunningScript script;
    memset(&script, 0, sizeof(CRunningScript));
    script.Init();
    script.m_bIsMission = true;
    script.m_bUseMissionCleanup = false;
    strcpy(script.m_szName, "coopand");
    script.m_pBaseIP = script.m_pCurrentIP = (uint8_t*)&header.opcode;

    lastOpCodeProcessed = header.opcode;

    // if (header.opcode == 0x0605 || header.opcode == 0x04ed || header.opcode == 0x04ef)
    //{
    //     //CChat::AddMessage("0x%04x", header.opcode);
    // }

    // TODO: refactor
    if (lastOpCodeProcessed == 0x0701)  // end_scene_skip
    {
        CHud::m_BigMessage[1][0] = 0;
    }
    else if (lastOpCodeProcessed == 0x02E7)  // start_cutscene
    {
        CutLog("rx start_cutscene status=%d running=%d name=%.8s", CCutsceneMgr::ms_cutsceneLoadStatus,
            CCutsceneMgr::ms_running, CCutsceneMgr::ms_cutsceneName);
        if (CCutsceneMgr::ms_cutsceneLoadStatus != 2)
        {
            COpCodeSync::ms_bLoadingCutscene = true;
            return;  // dont process opcode
        }
    }
    else if (lastOpCodeProcessed == 0x02E4)  // load_cutscene
    {
        CutLog("rx load_cutscene name=%.8s status=%d", textParamBuffer[0], CCutsceneMgr::ms_cutsceneLoadStatus);
        CCutsceneKeepSeat::BeforeNetworkLoad();  // loading the scene takes this player out of his car
        CCutsceneSkipVote::ms_bNetworkCutscene = true;
    }
    else if (lastOpCodeProcessed == 0x02EA)  // clear_cutscene
    {
        CutLog("rx clear_cutscene running=%d", CCutsceneMgr::ms_running);
        CCutsceneSkipVote::ms_bNetworkCutscene = false;
    }
    else if (lastOpCodeProcessed == COMMAND_CLEAR_MISSION_AUDIO && scriptParamCount >= 1)
    {
        MissionAudioHooks::HandleNetworkClear((uint8_t)(scriptParamsBuffer[0].value - 1));
    }

    switch (header.opcode)
    {
        case COMMAND_TASK_PLAY_ANIM:
        case COMMAND_TASK_CHAR_SLIDE_TO_COORD_AND_PLAY_ANIM:
        case COMMAND_TASK_PLAY_ANIM_NON_INTERRUPTABLE:
        case COMMAND_TASK_DIE_NAMED_ANIM:
        case COMMAND_TASK_PLAY_ANIM_WITH_FLAGS:
        case COMMAND_TASK_PLAY_ANIM_SECONDARY:
        {
            char* animlib = textParamBuffer[1];
            int id = CAnimManager::GetAnimationBlockIndex(animlib);
            if (id != -1)
            {
                if (!CAnimManager::ms_aAnimBlocks[id].bLoaded)
                {
                    CStreaming::RequestModel(
                        25575 + id, eStreamingFlags::MISSION_REQUIRED | eStreamingFlags::GAME_REQUIRED);
                    CStreaming::LoadAllRequestedModels(false);

                    if (COpCodeSync::ms_bProcessingTaskSequence)
                    {
                        CTaskSequenceSync::ms_bFailedToProcessSequence = true;
                    }
                    else
                    {
                        CNetworkAnimQueue::AddOpCode(buffer, bufferSize);
                    }

                    return;
                }
            }
            break;
        }
    }

    if (CHighStakesRace::OwnMarkers() && (header.opcode == 0x01B4 || header.opcode == 0x0158 ||
        header.opcode == 0x0159 || header.opcode == 0x015F || header.opcode == 0x0160 ||
        header.opcode == 0x02EB || header.opcode == 0x0373 || header.opcode == COMMAND_SWITCH_WIDESCREEN)) return;

    // mission mirror layer, receiver side: the authority's text table for the keys of a replayed print; load_mission_text
    // only names that table (CMissionMirror::OnReplayingCommand)
    if (!CMissionMirror::OnReplayingCommand(buffer, bufferSize))
    {
        return;
    }

    argCount = 0;

    if ((header.opcode & 0x7FFF) == 0x016A)
        CCameraMirror::OnMirroredFade();

    // clear_area (0395) replayed from the authority: CWorld::ClearExcitingStuffFromArea (gta-reversed World.cpp 0x56A0D0)
    // removes every vehicle that is not a mission/permanent one and has no follower of the player's group inside, and
    // the car this guest drives is neither (the authority's own car is a mission one): it would vanish with him in it.
    // The first clear_area of a mission often arrives while the guest still sits in the car, but the next one (High Stakes
    // CESAR1: after the CESAR1A cutscene, which warps everybody out of the car) finds it empty beside Cesar's marker.
    // So every car this client drove recently is protected, not only the one he is in now. Mark them as mission vehicles
    // only while the command runs.
    CVehicle* keepCars[RECENT_CARS + 1] = {};
    uint8_t keepCreators[RECENT_CARS + 1] = {};
    if ((header.opcode & 0x7FFF) == 0x0395 && CConfigLoader::ms_bGuestClearAreaKeepCar)
    {
        CPlayerPed* localPlayer = FindPlayerPed(0);
        CVehicle* candidates[RECENT_CARS + 1] = {localPlayer && localPlayer->m_nPedFlags.bInVehicle ? localPlayer->m_pVehicle : nullptr};
        for (int i = 0; i < RECENT_CARS; ++i)
            candidates[i + 1] = recentCarRefs[i] >= 0 ? CPools::GetVehicle(recentCarRefs[i]) : nullptr;
        int kept = 0;
        for (CVehicle* car : candidates)
        {
            if (!car || !CPools::ms_pVehiclePool->IsObjectValid(car) || !car->IsVTableValid() || car->m_nCreatedBy == MISSION_VEHICLE)
                continue;
            bool dup = false;
            for (int i = 0; i < kept; ++i)
                dup |= keepCars[i] == car;
            if (dup)
                continue;
            keepCars[kept] = car;
            keepCreators[kept++] = car->m_nCreatedBy;
            car->m_nCreatedBy = MISSION_VEHICLE;
        }
    }

    bProcessingNetworkOpcode = true;
    patch::RedirectJump(0x464080, CRunningScript__CollectParameters_Hook_SwitchParametersContext, false);
    patch::RedirectJump(0x463D50, CRunningScript__ReadTextLabelFromScript_Hook_SwitchParametersContext, false);

    if ((header.opcode & 0x7FFF) < CCustomCommandMgr::MIN_CUSTOM_COMMAND)
    {
        // CommandHandlerTable has 27 entries; ignore out-of-range opcodes from the network
        if ((header.opcode & 0x7FFF) / 100 < 27)
            CRunningScript::CommandHandlerTable[(header.opcode & 0x7FFF) / 100](&script, header.opcode & 0x7FFF);
    }
    else
    {
        CCustomCommandMgr::ProcessCommand((header.opcode & 0x7FFF), &script);
    }

    patch::SetRaw(0x464080, (void*)"\x66\x8B\x44\x24\x04", 5, false);
    patch::SetRaw(0x463D50, (void*)"\x8B\x41\x14\x83\xEC\x08", 6, false);
    bProcessingNetworkOpcode = false;

    for (int i = 0; i < RECENT_CARS + 1; ++i)
    {
        if (keepCars[i] && CPools::ms_pVehiclePool->IsObjectValid(keepCars[i]))
            keepCars[i]->m_nCreatedBy = keepCreators[i];
    }
}

static void PoolBeforeCommand()
{
    CActivityChallenge::BeforeCommand(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed) & 0x7FFF);
    CCoopPool::BeforeCommand(lastProcessedScript, static_cast<uint16_t>(lastOpCodeProcessed));
}

void __declspec(naked) FinishedOpcodeProcessing_Hook()
{
    __asm
    {
        mov lastOpCodeProcessed, ecx
        mov lastProcessedScript, esi
        mov scriptRunningCommand, esi

        pushad
        call PoolBeforeCommand
        popad

        mov ecx, esi
        call edx

        push eax
        call BuildAndSendOpcode
        mov scriptRunningCommand, 0
        pop eax

        test al, al

        push 0x469FF9
        ret
    }
}

static uint16_t argParamCount = 0;
static unsigned localCommandDepth = 0;
static uint16_t savedLocalArgCount = 0;
void COpCodeSync::BeginLocalCommands()
{
    if (localCommandDepth++ == 0) savedLocalArgCount = argParamCount;
}
void COpCodeSync::EndLocalCommands()
{
    if (localCommandDepth && --localCommandDepth == 0) argParamCount = savedLocalArgCount;
}
bool COpCodeSync::ProbeCondition(CRunningScript* source, uint16_t opcode, const int* params, int count)
{
    // Only read-only handlers used by T63 may enter here.
    const int expected = opcode == 0x02E0 ? 1 : opcode == 0x02D6 ? 6 :
        (opcode == 0x00FE || opcode == 0x00FF || opcode == 0x0102) ? 8 :
        (opcode == 0x051A || opcode == 0x051C || opcode == 0x09ED || opcode == 0x00DB) ? 2 :
        opcode == 0x05FE ? 9 : 0;  // 00DB / 05FE: IS_CHAR_IN_CAR / IS_CHAR_IN_ANGLED_AREA_IN_CAR_3D (D, Cop Wheels)
    if (!source || !params || !expected || count != expected)
        return false;
    uint8_t code[9 * 5]{};
    for (int i = 0; i < count; ++i)
    {
        code[i * 5] = 1; // immediate int32, also preserves float parameter bits
        memcpy(code + i * 5 + 1, &params[i], 4);
    }
    int savedParams[32];
    memcpy(savedParams, ScriptParams, sizeof(savedParams));
    OpcodeParameter savedBuffer[NUM_SYNCED_PARAMS];
    memcpy(savedBuffer, scriptParamsBuffer, sizeof(savedBuffer));
    const auto savedCount = scriptParamCount;
    const auto savedArgCount = argParamCount;
    CRunningScript probe = *source;
    probe.m_pCurrentIP = code;
    probe.m_bNotFlag = false;
    probe.m_nLogicalOp = 0;
    probe.m_bCondResult = false;
    CRunningScript::CommandHandlerTable[opcode / 100](&probe, opcode);
    const bool result = probe.m_bCondResult;
    memcpy(ScriptParams, savedParams, sizeof(savedParams));
    memcpy(scriptParamsBuffer, savedBuffer, sizeof(savedBuffer));
    scriptParamCount = savedCount;
    argParamCount = savedArgCount;
    return result;
}

static void CollectParamsProperly()
{
    if (scriptParamCount >= NUM_SYNCED_PARAMS)
        return;
    uint16_t freeSlots = (uint16_t)(NUM_SYNCED_PARAMS - scriptParamCount);
    uint16_t toCopy = (argParamCount < freeSlots) ? argParamCount : freeSlots;
    for (uint16_t i = 0; i < toCopy; i++)
    {
        COpCodeSync::scriptParamsBuffer[scriptParamCount + i].SetValue(ScriptParams[i]);
    }

    scriptParamCount += toCopy;
}

void __declspec(naked) CRunningScript__CollectParameters_Hook_GetSyncingParams()
{
    __asm
    {
        pop ebx
        mov eax, [esp + 0x4]
        mov argParamCount, ax

        pop eax
        add esp, 4

        pushad
    }

    if (localCommandDepth)
    {
        // Keep the enclosing opcode capture intact during local native calls.
    }
    else if (lastProcessedScript && (COpCodeSync::IsOpcodeSyncable(lastOpCodeProcessed) ||
                                   CTaskSequenceSync::IsNeededToCollectParametes((eScriptCommands)lastOpCodeProcessed)))
    {
        CollectParamsProperly();

        /*if (lastOpCodeProcessed == 0x0605)
        {
            CChat::AddMessage("arg0 %d", scriptParamsBuffer[0].value);
        }*/
    }
    else
    {
        scriptParamCount = 0;
        argParamCount = 0;
    }

    __asm
    {
        popad
        jmp eax
    }
}

static char* textPointer;
static uint8_t textLength;
void CollectTextParameters()
{
    if (localCommandDepth) return;
    if (textPointer && lastProcessedScript &&
        (COpCodeSync::IsOpcodeSyncable(lastOpCodeProcessed) ||
            CTaskSequenceSync::IsNeededToCollectParametes((eScriptCommands)lastOpCodeProcessed)))
    {
        if (textParamCount >= NUM_SYNCED_PARAMS)
        {
            // never write past the fixed buffers: drop the whole pending command instead
            memset(textParamBuffer, 0, sizeof textParamBuffer);
            memset(textLengthBuffer, 0, sizeof textLengthBuffer);
            scriptParamCount = 0;
            textParamCount = 0;
            return;
        }
        textLengthBuffer[textParamCount] = VMIN(textLength, strlen(textPointer));
        strncpy_s(textParamBuffer[textParamCount], textPointer, textLengthBuffer[textParamCount]);
        textParamCount++;
    }
}

void __declspec(naked) CRunningScript__ReadTextLabelFromScript_Hook_GetSyncingParams()
{
    __asm
        {
        add esp, 8

        mov eax, [esp + 0x4]
        mov textPointer, eax

        mov eax, [esp + 0x8]
        mov textLength, al

        call CollectTextParameters

        retn 8
        }
}

static uintptr_t FinishedProcessingScripts_ptr = 0x0;
static void FinishedProcessingScripts()
{
    plugin::CallDyn(FinishedProcessingScripts_ptr);
    CMissionMirror::OnScriptsProcessed(); // mission start/end tracking

    if (CLocalPlayer::m_bIsHost)
    {
        if (CEntryExitMarkerSync::ms_bUpdateAfterProcessingScripts)
        {
            CEntryExitMarkerSync::ms_bUpdateAfterProcessingScripts = false;
            CEntryExitMarkerSync::Send();
        }
    }
    CMissionSyncRules::SendHoodDoorCorrection();
}

void COpCodeSync::Init()
{
    DWORD temp;
    injector::UnprotectMemory(0x464080, 5, temp);
    injector::UnprotectMemory(0x463D50, 6, temp);

    patch::RedirectJump(0x469FF3, FinishedOpcodeProcessing_Hook);

    std::vector<int> hookPositions = {
        0x463D92, 0x463DBF, 0x463E1A, 0x463E6B, 0x463EAC, 0x463F2A, 0x463F7F, 0x463FA2, 0x463FDA, 0x463FFB, 0x464034};
    // the last exit (0x46404D, after the strncpy_s at 0x464042) serves SCRIPT_PARAM_LOCAL_SHORT_STRING_VARIABLE (the jump at
    // 0x463DDC) and the long local strings of 16+ chars: without it a PRINT_NOW of 237@s (DRUGS1's dialogue, 29 scripts)
    // went out with no text and the guests never saw it (gta-reversed CRunningScript::ReadTextLabelFromScript, dumpbin)
    if (CConfigLoader::ms_bSyncLocalTextVars)
        patch::RedirectJump(0x46404D, CRunningScript__ReadTextLabelFromScript_Hook_GetSyncingParams);
    patch::RedirectJump(hookPositions, CRunningScript__ReadTextLabelFromScript_Hook_GetSyncingParams);
    patch::RedirectJump(0x46421D, CRunningScript__CollectParameters_Hook_GetSyncingParams);

    FinishedProcessingScripts_ptr = injector::GetBranchDestination(0x46A22E).as_int();
    patch::RedirectCall(0x46A22E, FinishedProcessingScripts);

    // trace the scene state changes of this client (see CutLog)
    Events::gameProcessEvent += []
    {
        TrackRecentCars();
        static int lastRunning = -1;
        static int lastStatus = -1;
        if ((int)CCutsceneMgr::ms_running != lastRunning || CCutsceneMgr::ms_cutsceneLoadStatus != lastStatus)
        {
            lastRunning = (int)CCutsceneMgr::ms_running;
            lastStatus = CCutsceneMgr::ms_cutsceneLoadStatus;
            CutLog("state running=%d loadStatus=%d name=%.8s", lastRunning, lastStatus, CCutsceneMgr::ms_cutsceneName);
        }
    };


}