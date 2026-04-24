# CSM-Addon-MassData-Parameter-Support

[English](./README.md) | [中文](./README(zh-cn).md)

[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=installs)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=stars)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![GitHub all releases](https://img.shields.io/github/downloads/NEVSTOP-LAB/CSM-MassData-Parameter-Support/total)](https://github.com/NEVSTOP-LAB/CSM-MassData-Parameter-Support/releases)

## Overview

CSM-MassData-Parameter-Support is an add-on for the Communicable State Machine (CSM) framework that enables efficient transfer of large datasets between CSM modules. Instead of encoding large data directly into the API string, it uses a reference mechanism to pass data by reference, overcoming the limitations of API strings for large data transfer.

## Why MassData Support?

In LabVIEW test and measurement applications, handling large data types such as waveforms and 1D/2D arrays is common, especially in high-sampling-rate and multi-channel systems. Transferring such data using traditional API string encoding is inefficient due to:

- Extra memory overhead from plaintext encoding
- Performance cost of encoding/decoding large data
- Excessive log noise that degrades debug readability

## How It Works

MassData Support is based on the following principle:

1. **Encode**: Convert large data into a compact reference string ("address") instead of encoding the data inline
2. **Transmit**: Send the reference string through CSM's invisible bus
3. **Decode**: The receiving CSM module uses the reference string to retrieve the original data

The reference string contains three fields — `flag`, `start position`, and `size` — which together act as a locator for the actual data stored in a shared memory buffer.

## Key Benefits

1. **Efficient Transmission**: Only a compact reference string is transferred, avoiding full data copies
2. **Memory Efficient**: Large data is stored once regardless of the number of receivers
3. **Cleaner Logs**: Compact reference strings keep CSM log output concise and readable

## Data Lifecycle

- MassData uses a circular buffer internally
- When the buffer is full, new data overwrites old data from the beginning
- Overwritten data cannot be recovered; decoding will fail
- All CSM modules in the same application share the same MassData buffer

## Best Practices

1. **Avoid Long-Lived Data**: Do not use MassData for data that must persist indefinitely
2. **Set an Appropriate Cache Size**: Use `Config MassData Parameter Cache Size.vi` to configure the buffer size
   - Too large wastes memory
   - Too small causes frequent overwrites
3. **Monitor Cache Usage**: Use the provided debugging tools to observe cache usage and tune the configuration

## Installation

Install via VIPM (VI Package Manager). After installation, find it in the CSM add-ons palette.

## Usage

1. Use encoding VIs to convert large data into MassData arguments
2. Pass these arguments between CSM modules via CSM's parameter passing mechanism
3. Use decoding VIs at the receiving end to retrieve the original data

## Examples

See the example folder for demonstrations of:

1. MassData argument format
2. Displaying MassData cache status on the front panel
3. Using MassData in non-CSM frameworks
4. Using MassData with CSM

## Development Environment

LabVIEW 2017 or later

## Third-party language bindings

A C-language port of the MassData API (function names, parameters and
reference-string format identical to the LabVIEW VIs) is available under
[`c/`](./c). It ships with a Visual Studio 2022 test project at
[`c/vs_test`](./c/vs_test).

## License

This project is licensed under the MIT License — see the LICENSE file for details.

