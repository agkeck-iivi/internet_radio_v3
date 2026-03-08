# introduction
The internet radio uses ir remote control to control the on/off state of the audio device to which it is connected.  During boot time the audio device is turned on.  One can also toggle the on/off state of the audio device by a double click on the volume button.  This code is entwined with the main logic of the application and should be extracted into a separate component.

# goal
Extract the the ir remote implementation into a component.  This component should export an abstract interface and should be designed to allow other ir remote protocols to be added easily.

# requirements
    * separate component for ir remote implementation
    * export functions to init, deinit, turn_audio_on, turn_audio_off, toggle_audio, enable_ir_remote, disable_ir_remote
    * use enum to identify the ir remote protocol
    * update main application to use this new component
    * make it possible to easily remove ir remote functionality entirely