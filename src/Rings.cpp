#include "plugin.hpp"
#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"
#include "rings/dsp/string_synth_part.h"


struct Rings : Module {
	enum ParamIds {
		POLYPHONY_PARAM,        // Button to cycle through polyphony modes (1, 2, 4 voices)
		RESONATOR_PARAM,        // Button to cycle through resonator models

		FREQUENCY_PARAM,        // Main frequency control (0.0-60.0 semitones, default 30.0)
		STRUCTURE_PARAM,        // Structure parameter (0.0-1.0, default 0.5) - inharmonicity or string intervals
		BRIGHTNESS_PARAM,       // Brightness parameter (0.0-1.0, default 0.5) - spectrum brightness
		DAMPING_PARAM,          // Damping parameter (0.0-1.0, default 0.5) - decay time (100ms to 10s)
		POSITION_PARAM,         // Position parameter (0.0-1.0, default 0.5) - excitation point

		BRIGHTNESS_MOD_PARAM,   // Brightness CV attenuverter (-1.0 to 1.0)
		FREQUENCY_MOD_PARAM,    // Frequency CV attenuverter (-1.0 to 1.0) for FM
		DAMPING_MOD_PARAM,      // Damping CV attenuverter (-1.0 to 1.0)
		STRUCTURE_MOD_PARAM,    // Structure CV attenuverter (-1.0 to 1.0)
		POSITION_MOD_PARAM,     // Position CV attenuverter (-1.0 to 1.0)
		NUM_PARAMS
	};
	enum InputIds {
		BRIGHTNESS_MOD_INPUT,   // CV input for brightness modulation
		FREQUENCY_MOD_INPUT,    // CV input for frequency modulation (FM)
		DAMPING_MOD_INPUT,      // CV input for damping modulation
		STRUCTURE_MOD_INPUT,    // CV input for structure modulation
		POSITION_MOD_INPUT,     // CV input for position modulation

		STRUM_INPUT,            // Trigger/gate input for strumming
		PITCH_INPUT,            // 1V/oct pitch CV input
		IN_INPUT,               // Audio input (accepts up to 16Vpp modular levels)
		NUM_INPUTS
	};
	enum OutputIds {
		ODD_OUTPUT,             // Odd partials output (mono) or odd voices output (poly)
		EVEN_OUTPUT,            // Even partials output (mono) or even voices output (poly)
		NUM_OUTPUTS             // Number of outputs
	};
	enum LightIds {
		POLYPHONY_GREEN_LIGHT, POLYPHONY_RED_LIGHT,    // Indicates polyphony mode (green=1, both=2, red=4)
		RESONATOR_GREEN_LIGHT, RESONATOR_RED_LIGHT,    // Indicates resonator model
		NUM_LIGHTS
	};

	// Sample rate conversion: converts between Cardinal's variable sample rate and Rings' fixed 48kHz
	dsp::SampleRateConverter<1> inputSrc;   // Converts input from Cardinal's sample rate to 48kHz
	dsp::SampleRateConverter<2> outputSrc;  // Converts output from 48kHz back to Cardinal's sample rate
	dsp::DoubleRingBuffer<dsp::Frame<1>, 256> inputBuffer;   // Buffers input samples (mono)
	dsp::DoubleRingBuffer<dsp::Frame<2>, 256> outputBuffer;  // Buffers output samples (stereo)

	// Shared reverb buffer (32KB) for both DSP engines
	uint16_t reverb_buffer[32768] = {};
	rings::Part part;                        // Main resonator processing engine
	rings::StringSynthPart string_synth;     // String synth easter egg mode
	rings::Strummer strummer;                // Handles strumming logic and note detection
	bool strum = false;                      // Current strum trigger state
	bool lastStrum = false;                  // Previous strum state for edge detection

	dsp::SchmittTrigger polyphonyTrigger;    // Schmitt trigger for polyphony button presses
	dsp::SchmittTrigger modelTrigger;        // Schmitt trigger for resonator model button presses
	int polyphonyMode = 0;                   // Current polyphony setting (0=1 voice, 1=2 voices, 2=4 voices)
	rings::ResonatorModel resonatorModel = rings::RESONATOR_MODEL_MODAL;  // Current resonator model
	bool easterEgg = false;                  // Boolean flag for "Disastrous Peace" string synth mode

	Rings() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(POLYPHONY_PARAM, "Polyphony");        // Cycles through 1, 2, 4 voice modes
		configButton(RESONATOR_PARAM, "Resonator type");   // Cycles through resonator models
		configParam(FREQUENCY_PARAM, 0.0, 60.0, 30.0, "Frequency");      // Base frequency transpose (semitones)
		configParam(STRUCTURE_PARAM, 0.0, 1.0, 0.5, "Structure");        // Inharmonicity or string intervals
		configParam(BRIGHTNESS_PARAM, 0.0, 1.0, 0.5, "Brightness");      // Spectrum brightness
		configParam(DAMPING_PARAM, 0.0, 1.0, 0.5, "Damping");            // Decay time (100ms to 10s)
		configParam(POSITION_PARAM, 0.0, 1.0, 0.5, "Position");          // Excitation point
		configParam(BRIGHTNESS_MOD_PARAM, -1.0, 1.0, 0.0, "Brightness CV");   // Attenuverter for brightness CV
		configParam(FREQUENCY_MOD_PARAM, -1.0, 1.0, 0.0, "Frequency CV");     // Attenuverter for FM CV
		configParam(DAMPING_MOD_PARAM, -1.0, 1.0, 0.0, "Damping CV");         // Attenuverter for damping CV
		configParam(STRUCTURE_MOD_PARAM, -1.0, 1.0, 0.0, "Structure CV");     // Attenuverter for structure CV
		configParam(POSITION_MOD_PARAM, -1.0, 1.0, 0.0, "Position CV");       // Attenuverter for position CV

		configInput(BRIGHTNESS_MOD_INPUT, "Brightness");   // CV input for brightness modulation
		configInput(FREQUENCY_MOD_INPUT, "Frequency");     // CV input for frequency modulation (FM)
		configInput(DAMPING_MOD_INPUT, "Damping");         // CV input for damping modulation
		configInput(STRUCTURE_MOD_INPUT, "Structure");     // CV input for structure modulation
		configInput(POSITION_MOD_INPUT, "Position");       // CV input for position modulation
		configInput(STRUM_INPUT, "Strum");                 // Trigger/gate input for strumming
		configInput(PITCH_INPUT, "Pitch (1V/oct)");        // 1V/oct pitch CV input
		configInput(IN_INPUT, "Audio");                    // Audio input (accepts up to 16Vpp modular levels)

		configOutput(ODD_OUTPUT, "Odd");   // Odd partials (mono) or odd voices (poly)
		configOutput(EVEN_OUTPUT, "Even"); // Even partials (mono) or even voices (poly)

		// Bypass routing: audio input routed to both outputs when bypassed
		configBypass(IN_INPUT, ODD_OUTPUT);
		configBypass(IN_INPUT, EVEN_OUTPUT);

		// Initialize DSP engines: strummer with 10ms IOI and sample rate, both engines share reverb buffer
		strummer.Init(0.01, 44100.0 / 24);  // 10ms inter-onset interval, sample rate for block processing
		part.Init(reverb_buffer);           // Initialize main resonator engine
		string_synth.Init(reverb_buffer);   // Initialize string synth easter egg engine
	}

	void process(const ProcessArgs& args) override {
		// TODO: "Normalized to a pulse/burst generator that reacts to note changes on the V/OCT input."
		
		// Input buffering: Audio input normalized from ±5V modular levels to ±1.0 for DSP processing
		if (!inputBuffer.full()) {
			dsp::Frame<1> f;
			f.samples[0] = inputs[IN_INPUT].getVoltage() / 5.0;  // Scale modular level to DSP range
			inputBuffer.push(f);
		}

		// Strum detection: STRUM input detected when voltage ≥ 1.0V (edge-triggered)
		if (!strum) {
			strum = inputs[STRUM_INPUT].getVoltage() >= 1.0;
		}

		// Polyphony/Model switching: Button presses cycle through modes using Schmitt triggers
		if (polyphonyTrigger.process(params[POLYPHONY_PARAM].getValue())) {
			polyphonyMode = (polyphonyMode + 1) % 3;  // Cycle: 0=1 voice, 1=2 voices, 2=4 voices
		}
		// Polyphony LED indicators: Green only (1 voice), Both (2 voices), Red only (4 voices)
		lights[POLYPHONY_GREEN_LIGHT].value = (polyphonyMode == 0 || polyphonyMode == 1) ? 1.0 : 0.0;
		lights[POLYPHONY_RED_LIGHT].value = (polyphonyMode == 1 || polyphonyMode == 2) ? 1.0 : 0.0;

		// Resonator model switching: Cycles through first 3 models (more available via context menu)
		if (modelTrigger.process(params[RESONATOR_PARAM].getValue())) {
			resonatorModel = (rings::ResonatorModel)((resonatorModel + 1) % 3);
		}
		// Resonator model LED indicators: Shows current model via color coding
		int modelColor = resonatorModel % 3;
		lights[RESONATOR_GREEN_LIGHT].value = (modelColor == 0 || modelColor == 1) ? 1.0 : 0.0;
		lights[RESONATOR_RED_LIGHT].value = (modelColor == 1 || modelColor == 2) ? 1.0 : 0.0;

		// Block processing: Process when output buffer is empty (DSP engine processes in 24-sample blocks at 48kHz)
		if (outputBuffer.empty()) {
			float in[24] = {};
			// Sample rate conversion: Convert input buffer from Cardinal's sample rate to 48kHz (24-sample blocks)
			{
				inputSrc.setRates(args.sampleRate, 48000);  // Set conversion rates
				int inLen = inputBuffer.size();
				int outLen = 24;  // Rings DSP processes in fixed 24-sample blocks
				inputSrc.process(inputBuffer.startData(), &inLen, (dsp::Frame<1>*) in, &outLen);
				inputBuffer.startIncr(inLen);  // Advance input buffer by consumed samples
			}

			// Polyphony configuration: Set polyphony (1, 2, or 4 voices) via bit shift
			int polyphony = 1 << polyphonyMode;  // 1 << 0 = 1, 1 << 1 = 2, 1 << 2 = 4
			if (part.polyphony() != polyphony)
				part.set_polyphony(polyphony);  // Update DSP engine polyphony
			
			// Resonator model configuration: Set model or easter egg FX type
			if (easterEgg)
				string_synth.set_fx((rings::FxType) resonatorModel);  // Easter egg: use model param for FX type
			else
				part.set_model(resonatorModel);  // Normal mode: set resonator model

			// Patch parameter construction: Build Patch structure with CV modulation applied
			rings::Patch patch;
			// Structure: CV uses quadraticBipolar curve for smooth modulation, scaled by 3.3x and normalized to ±5V
			float structure = params[STRUCTURE_PARAM].getValue() + 3.3 * dsp::quadraticBipolar(params[STRUCTURE_MOD_PARAM].getValue()) * inputs[STRUCTURE_MOD_INPUT].getVoltage() / 5.0;
			patch.structure = clamp(structure, 0.0f, 0.9995f);  // Clamp to valid range
			// Brightness: Quadratic bipolar curve, clamped to 0.0-1.0
			patch.brightness = clamp(params[BRIGHTNESS_PARAM].getValue() + 3.3 * dsp::quadraticBipolar(params[BRIGHTNESS_MOD_PARAM].getValue()) * inputs[BRIGHTNESS_MOD_INPUT].getVoltage() / 5.0, 0.0f, 1.0f);
			// Damping: Quadratic bipolar curve, clamped to 0.0-0.9995 (maps to 100ms-10s decay)
			patch.damping = clamp(params[DAMPING_PARAM].getValue() + 3.3 * dsp::quadraticBipolar(params[DAMPING_MOD_PARAM].getValue()) * inputs[DAMPING_MOD_INPUT].getVoltage() / 5.0, 0.0f, 0.9995f);
			// Position: Quadratic bipolar curve, clamped to 0.0-0.9995
			patch.position = clamp(params[POSITION_PARAM].getValue() + 3.3 * dsp::quadraticBipolar(params[POSITION_MOD_PARAM].getValue()) * inputs[POSITION_MOD_INPUT].getVoltage() / 5.0, 0.0f, 0.9995f);

			// Performance state construction: Build PerformanceState with note, frequency, and control flags
			rings::PerformanceState performance_state;
			// Pitch CV: Convert 1V/oct to MIDI note (12 semitones per volt), normalized to 1/12V default
			performance_state.note = 12.0 * inputs[PITCH_INPUT].getNormalVoltage(1 / 12.0);
			float transpose = params[FREQUENCY_PARAM].getValue();  // Base frequency transpose (0-60 semitones)
			// Transpose quantization: When pitch input is connected, transpose is rounded to nearest semitone
			if (inputs[PITCH_INPUT].isConnected()) {
				transpose = roundf(transpose);
			}
			// Tonic: Base frequency in MIDI note format (12.0 = C0, clamped 0-60 semitones)
			performance_state.tonic = 12.0 + clamp(transpose, 0.0f, 60.0f);
			// Frequency modulation: Uses quarticBipolar curve for FM synthesis (±48 semitones range)
			performance_state.fm = clamp(48.0 * 3.3 * dsp::quarticBipolar(params[FREQUENCY_MOD_PARAM].getValue()) * inputs[FREQUENCY_MOD_INPUT].getNormalVoltage(1.0) / 5.0, -48.0f, 48.0f);

			// Internal mode flags: Set based on input connections (inverted logic)
			performance_state.internal_exciter = !inputs[IN_INPUT].isConnected();   // Use internal exciter if no audio input
			performance_state.internal_strum = !inputs[STRUM_INPUT].isConnected();  // Use internal strum detection if no strum input
			performance_state.internal_note = !inputs[PITCH_INPUT].isConnected();   // Use internal note generation if no pitch input

			// TODO: "Normalized to a step detector on the V/OCT input and a transient detector on the IN input."
			// Strum trigger: Edge detection - trigger on rising edge (strum && !lastStrum)
			performance_state.strum = strum && !lastStrum;  // Rising edge detection
			lastStrum = strum;  // Store current state for next frame
			strum = false;       // Reset strum flag for next detection cycle

			// Chord selection: Map structure parameter to chord index (0-10) for quantized sympathetic strings
			performance_state.chord = clamp((int) roundf(structure * (rings::kNumChords - 1)), 0, rings::kNumChords - 1);

			// Audio processing: Process through DSP engine (24-sample blocks at 48kHz)
			float out[24];  // Main output buffer
			float aux[24];  // Auxiliary output buffer
			if (easterEgg) {
				// Easter egg mode: String synth processing (no input to strummer, uses internal note detection)
				strummer.Process(NULL, 24, &performance_state);  // NULL input for internal note detection
				string_synth.Process(performance_state, patch, in, out, aux, 24);
			}
			else {
				// Normal mode: Main resonator processing with strummer for note/onset detection
				strummer.Process(in, 24, &performance_state);  // Process input for onset detection
				part.Process(performance_state, patch, in, out, aux, 24);  // Main DSP processing
			}

			// Output sample rate conversion: Convert from 48kHz back to Cardinal's sample rate
			{
				dsp::Frame<2> outputFrames[24];  // Pack stereo output into frames
				for (int i = 0; i < 24; i++) {
					outputFrames[i].samples[0] = out[i];   // Odd partials/voices
					outputFrames[i].samples[1] = aux[i];   // Even partials/voices
				}

				outputSrc.setRates(48000, args.sampleRate);  // Set conversion rates (from 48kHz to Cardinal's rate)
				int inLen = 24;  // Input is 24 samples
				int outLen = outputBuffer.capacity();  // Output fills available buffer space
				outputSrc.process(outputFrames, &inLen, outputBuffer.endData(), &outLen);
				outputBuffer.endIncr(outLen);  // Advance output buffer by produced samples
			}
		}

		// Output routing: Set output voltages, matching hardware behavior for single/multiple output connections
		if (!outputBuffer.empty()) {
			dsp::Frame<2> outputFrame = outputBuffer.shift();  // Get next output frame
			// Hardware behavior: "Note that you need to insert a jack into each output to split the signals:
			// when only one jack is inserted, both signals are mixed together."
			if (outputs[ODD_OUTPUT].isConnected() && outputs[EVEN_OUTPUT].isConnected()) {
				// Both outputs connected: Split odd/even signals (mono: partials, poly: voices)
				outputs[ODD_OUTPUT].setVoltage(clamp(outputFrame.samples[0], -1.0, 1.0) * 5.0);   // Scale DSP range to ±5V
				outputs[EVEN_OUTPUT].setVoltage(clamp(outputFrame.samples[1], -1.0, 1.0) * 5.0);  // Scale DSP range to ±5V
			}
			else {
				// Single output connected: Mix both signals together (hardware normalization behavior)
				float v = clamp(outputFrame.samples[0] + outputFrame.samples[1], -1.0, 1.0) * 5.0;
				outputs[ODD_OUTPUT].setVoltage(v);   // Send mixed signal to both outputs
				outputs[EVEN_OUTPUT].setVoltage(v);  // Send mixed signal to both outputs
			}
		}
	}

	// Serialization: Save module state (polyphony, model, easter egg) to JSON
	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_object_set_new(rootJ, "polyphony", json_integer(polyphonyMode));        // Save current polyphony mode
		json_object_set_new(rootJ, "model", json_integer((int) resonatorModel));     // Save current resonator model
		json_object_set_new(rootJ, "easterEgg", json_boolean(easterEgg));            // Save easter egg mode state

		return rootJ;
	}

	// Deserialization: Load module state from JSON
	void dataFromJson(json_t* rootJ) override {
		json_t* polyphonyJ = json_object_get(rootJ, "polyphony");
		if (polyphonyJ) {
			polyphonyMode = json_integer_value(polyphonyJ);  // Restore polyphony mode
		}

		json_t* modelJ = json_object_get(rootJ, "model");
		if (modelJ) {
			resonatorModel = (rings::ResonatorModel) json_integer_value(modelJ);  // Restore resonator model
		}

		json_t* easterEggJ = json_object_get(rootJ, "easterEgg");
		if (easterEggJ) {
			easterEgg = json_boolean_value(easterEggJ);  // Restore easter egg mode
		}
	}

	// Reset: Restore default state (1 voice, modal resonator)
	void onReset() override {
		polyphonyMode = 0;  // Reset to 1 voice
		resonatorModel = rings::RESONATOR_MODEL_MODAL;  // Reset to modal resonator
	}

	// Randomize: Set random polyphony and resonator model
	void onRandomize() override {
		polyphonyMode = random::u32() % 3;  // Random polyphony (0-2)
		resonatorModel = (rings::ResonatorModel)(random::u32() % 3);  // Random model (0-2)
	}
};


// UI Widget: Defines the visual layout and controls for the Rings module
struct RingsWidget : ModuleWidget {
	RingsWidget(Rings* module) {
		setModule(module);
		setPanel(Svg::load(asset::plugin(pluginInstance, "res/Rings.svg")));  // Load panel SVG

		// Panel screws: Four corner screws for mounting
		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(180, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(180, 365)));

		// Buttons: Two buttons for polyphony and resonator model selection
		addParam(createParam<TL1105>(Vec(14, 40), module, Rings::POLYPHONY_PARAM));    // Polyphony button
		addParam(createParam<TL1105>(Vec(179, 40), module, Rings::RESONATOR_PARAM));   // Resonator model button

		// Large knobs: Two main controls (frequency and structure)
		addParam(createParam<Rogan3PSWhite>(Vec(29, 72), module, Rings::FREQUENCY_PARAM));   // Frequency knob
		addParam(createParam<Rogan3PSWhite>(Vec(126, 72), module, Rings::STRUCTURE_PARAM));  // Structure knob

		// Small knobs: Three secondary controls (brightness, damping, position)
		addParam(createParam<Rogan1PSWhite>(Vec(13, 158), module, Rings::BRIGHTNESS_PARAM)); // Brightness knob
		addParam(createParam<Rogan1PSWhite>(Vec(83, 158), module, Rings::DAMPING_PARAM));    // Damping knob
		addParam(createParam<Rogan1PSWhite>(Vec(154, 158), module, Rings::POSITION_PARAM));  // Position knob

		// Trim pots: Five CV attenuverters for modulation inputs
		addParam(createParam<Trimpot>(Vec(19, 229), module, Rings::BRIGHTNESS_MOD_PARAM));   // Brightness CV attenuverter
		addParam(createParam<Trimpot>(Vec(57, 229), module, Rings::FREQUENCY_MOD_PARAM));    // Frequency CV attenuverter
		addParam(createParam<Trimpot>(Vec(96, 229), module, Rings::DAMPING_MOD_PARAM));      // Damping CV attenuverter
		addParam(createParam<Trimpot>(Vec(134, 229), module, Rings::STRUCTURE_MOD_PARAM));   // Structure CV attenuverter
		addParam(createParam<Trimpot>(Vec(173, 229), module, Rings::POSITION_MOD_PARAM));    // Position CV attenuverter

		// CV inputs: Five modulation inputs
		addInput(createInput<PJ301MPort>(Vec(15, 273), module, Rings::BRIGHTNESS_MOD_INPUT));  // Brightness CV
		addInput(createInput<PJ301MPort>(Vec(54, 273), module, Rings::FREQUENCY_MOD_INPUT));   // Frequency CV (FM)
		addInput(createInput<PJ301MPort>(Vec(92, 273), module, Rings::DAMPING_MOD_INPUT));     // Damping CV
		addInput(createInput<PJ301MPort>(Vec(131, 273), module, Rings::STRUCTURE_MOD_INPUT));  // Structure CV
		addInput(createInput<PJ301MPort>(Vec(169, 273), module, Rings::POSITION_MOD_INPUT));   // Position CV

		// Audio I/O: Three inputs (strum, pitch, audio) and two outputs (odd, even)
		addInput(createInput<PJ301MPort>(Vec(15, 316), module, Rings::STRUM_INPUT));    // Strum trigger input
		addInput(createInput<PJ301MPort>(Vec(54, 316), module, Rings::PITCH_INPUT));    // Pitch CV input (1V/oct)
		addInput(createInput<PJ301MPort>(Vec(92, 316), module, Rings::IN_INPUT));       // Audio input
		addOutput(createOutput<PJ301MPort>(Vec(131, 316), module, Rings::ODD_OUTPUT));  // Odd partials/voices output
		addOutput(createOutput<PJ301MPort>(Vec(169, 316), module, Rings::EVEN_OUTPUT)); // Even partials/voices output

		// LED indicators: Two bicolor LEDs for polyphony and resonator model status
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(37, 43), module, Rings::POLYPHONY_GREEN_LIGHT));  // Polyphony LED
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(162, 43), module, Rings::RESONATOR_GREEN_LIGHT)); // Resonator LED
	}

	// Context menu: Adds resonator model selection and easter egg toggle
	void appendContextMenu(Menu* menu) override {
		Rings* module = dynamic_cast<Rings*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator);

		menu->addChild(createMenuLabel("Resonator"));  // Section header

		// Resonator model labels: All 6 models (3 standard + 3 bonus)
		static const std::vector<std::string> modelLabels = {
			"Modal resonator",                    // RESONATOR_MODEL_MODAL
			"Sympathetic strings",                // RESONATOR_MODEL_SYMPATHETIC_STRING
			"Modulated/inharmonic string",        // RESONATOR_MODEL_STRING
			"FM voice",                           // RESONATOR_MODEL_FM_VOICE
			"Quantized sympathetic strings",      // RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED
			"Reverb string",                      // RESONATOR_MODEL_STRING_AND_REVERB
		};
		// Create check menu items for each resonator model
		for (int i = 0; i < 6; i++) {
			menu->addChild(createCheckMenuItem(modelLabels[i], "",
				[=]() {return module->resonatorModel == i;},                    // Check if current model
				[=]() {module->resonatorModel = (rings::ResonatorModel) i;}     // Set model on selection
			));
		}

		menu->addChild(new MenuSeparator);

		// Easter egg toggle: "Disastrous Peace" string synth mode
		menu->addChild(createBoolMenuItem("Disastrous Peace", "",
			[=]() {return module->easterEgg;},        // Check if enabled
			[=](bool val) {module->easterEgg = val;}  // Toggle easter egg mode
		));
	}
};


Model* modelRings = createModel<Rings, RingsWidget>("Rings");
