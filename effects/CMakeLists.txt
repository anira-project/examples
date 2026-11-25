# SPDX-License-Identifier: Apache-2.0

# One folder per example: Example.h maps a model wrapper onto the targets (anira_example,
# cmake/examples.cmake). The small effects compile their weights in; the RAVE ones copy them into
# the bundle.

if(ANIRA_EXAMPLES_WITH_EFFECTS)
    anira_example(steerable-nafx
        HEADER steerable-nafx/Example.h
        LINK anira::models::steerable_nafx
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/steerable-nafx/steerable-nafx.catalog.json
        PRODUCT "Steerable NAFX"
        CODE Snfx
        EMBED)
    anira_example(guitar-lstm
        HEADER guitar-lstm/Example.h
        LINK anira::models::guitar_lstm
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/guitar-lstm/guitar-lstm.catalog.json
        PRODUCT "GuitarLSTM"
        CODE Glst
        EMBED)
    anira_example(silero-vad
        HEADER silero-vad/Example.h
        LINK anira::models::silero_vad
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/silero-vad/silero-vad.catalog.json
        PRODUCT "Silero VAD"
        CODE Svad
        EMBED)
    anira_example(stateful-lstm
        HEADER stateful-lstm/Example.h
        LINK anira::models::stateful_lstm
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/stateful-lstm/stateful-lstm.catalog.json
        PRODUCT "Stateful LSTM"
        CODE Slst
        EMBED)
endif()

if(ANIRA_EXAMPLES_WITH_RAVE)
    anira_example(rave-funk-drum
        HEADER rave-funk-drum/Example.h
        LINK anira::models::rave_funk_drum
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/rave-funk-drum/rave-funk-drum.catalog.json
        PRODUCT "RAVE Funk Drum"
        CODE Rfdr)
    anira_example(rave-djembe
        HEADER rave-djembe/Example.h
        LINK anira::models::rave_djembe
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/rave-djembe/rave-djembe.catalog.json
        PRODUCT "RAVE Djembe"
        CODE Rdjb)
    anira_example(rave-magnetic-resonator-piano
        HEADER rave-magnetic-resonator-piano/Example.h
        LINK anira::models::rave_magnetic_resonator_piano
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/rave-magnetic-resonator-piano/rave-magnetic-resonator-piano.catalog.json
        PRODUCT "RAVE Magnetic Resonator Piano"
        CODE Rmrp)
    anira_example(rave-funk-drum-latents
        HEADER rave-funk-drum-latents/Example.h
        LINK anira::models::rave_funk_drum
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/rave-funk-drum/rave-funk-drum.catalog.json
        PRODUCT "RAVE Funk Drum Latents"
        CODE Rfdl)
endif()

# The hand examples (stage 3): models' hand tracking on the camera, alone and driving a RAVE
# decoder. Camera examples: the JUCE target only.
if(ANIRA_EXAMPLES_WITH_HANDS)
    add_library(anira_examples_hands INTERFACE)
    target_include_directories(anira_examples_hands INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/hands/include)
    target_link_libraries(anira_examples_hands INTERFACE anira::models::handtracking)

    set(_mediapipe ${ANIRA_EXAMPLES_MODELS_DIR}/models/handtracking/mediapipe-gesture-recognizer.catalog.json)
    anira_example(hand-tracking
        HEADER hand-tracking/Example.h
        LINK anira_examples_hands
        CATALOG ${_mediapipe}
        PRODUCT "Hand Tracking"
        CODE Hand
        EMBED
        CAMERA)
    anira_example(hand-synth-piano
        HEADER hand-synth-piano/Example.h
        LINK anira_examples_hands anira::models::rave_magnetic_resonator_piano
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/rave-magnetic-resonator-piano/rave-magnetic-resonator-piano.catalog.json
                ${_mediapipe}
        PRODUCT "Hand Synth Piano"
        CODE Hsyp
        CAMERA)
    anira_example(hand-synth-djembe
        HEADER hand-synth-djembe/Example.h
        LINK anira_examples_hands anira::models::rave_djembe
        CATALOG ${ANIRA_EXAMPLES_MODELS_DIR}/models/rave-djembe/rave-djembe.catalog.json ${_mediapipe}
        PRODUCT "Hand Synth Djembe"
        CODE Hsyd
        CAMERA)
endif()
