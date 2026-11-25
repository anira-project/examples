# SPDX-License-Identifier: Apache-2.0

# One folder per example: Example.h maps a model wrapper onto the targets (anira_example,
# cmake/examples.cmake). The small effects compile their weights in; the RAVE ones copy them into
# the bundle.

if(ANIRA_EXAMPLES_WITH_EFFECTS)
    anira_example(steerable-nafx
        HEADER steerable-nafx/Example.h
        LINK anira::models::steerable_nafx
        CATALOG ${PROJECT_SOURCE_DIR}/models/models/steerable-nafx/steerable-nafx.catalog.json
        PRODUCT "Steerable NAFX"
        CODE Snfx
        EMBED)
    anira_example(guitar-lstm
        HEADER guitar-lstm/Example.h
        LINK anira::models::guitar_lstm
        CATALOG ${PROJECT_SOURCE_DIR}/models/models/guitar-lstm/guitar-lstm.catalog.json
        PRODUCT "GuitarLSTM"
        CODE Glst
        EMBED)
    anira_example(stateful-lstm
        HEADER stateful-lstm/Example.h
        LINK anira::models::stateful_lstm
        CATALOG ${PROJECT_SOURCE_DIR}/models/models/stateful-lstm/stateful-lstm.catalog.json
        PRODUCT "Stateful LSTM"
        CODE Slst
        EMBED)
endif()

if(ANIRA_EXAMPLES_WITH_RAVE)
    anira_example(rave-funk-drum
        HEADER rave-funk-drum/Example.h
        LINK anira::models::rave_funk_drum
        CATALOG ${PROJECT_SOURCE_DIR}/models/models/rave-funk-drum/rave-funk-drum.catalog.json
        PRODUCT "RAVE Funk Drum"
        CODE Rfdr)
    anira_example(rave-djembe
        HEADER rave-djembe/Example.h
        LINK anira::models::rave_djembe
        CATALOG ${PROJECT_SOURCE_DIR}/models/models/rave-djembe/rave-djembe.catalog.json
        PRODUCT "RAVE Djembe"
        CODE Rdjb)
    anira_example(rave-magnetic-resonator-piano
        HEADER rave-magnetic-resonator-piano/Example.h
        LINK anira::models::rave_magnetic_resonator_piano
        CATALOG ${PROJECT_SOURCE_DIR}/models/models/rave-magnetic-resonator-piano/rave-magnetic-resonator-piano.catalog.json
        PRODUCT "RAVE Magnetic Resonator Piano"
        CODE Rmrp)
    anira_example(rave-funk-drum-latents
        HEADER rave-funk-drum-latents/Example.h
        LINK anira::models::rave_funk_drum
        CATALOG ${PROJECT_SOURCE_DIR}/models/models/rave-funk-drum/rave-funk-drum.catalog.json
        PRODUCT "RAVE Funk Drum Latents"
        CODE Rfdl)
endif()
