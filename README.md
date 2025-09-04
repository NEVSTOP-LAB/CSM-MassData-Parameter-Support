# CSM-Addon-MassData-Parameter-Support

[English](./README.md) | [中文](./README(zh-cn).md)

[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=installs)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![Image](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/badge.svg?metric=stars)](https://www.vipm.io/package/nevstop_lib_csm_massdata_parameter_support/)
[![GitHub all releases](https://img.shields.io/github/downloads/NEVSTOP-LAB/CSM-MassData-Parameter-Support/total)](https://github.com/NEVSTOP-LAB/CSM-MassData-Parameter-Support/releases)

## Overview

CSM-MassData-Parameter-Support is an addon for the Communicable State Machine (CSM) framework that enables efficient transfer of large data sets between CSM modules. It addresses the limitations of API String for transferring large data by using a memory-efficient mechanism to reference rather than directly encode large data structures.

## Why MassData Support?

In LabVIEW test and measurement applications, handling large data types such as waveforms, 1D/2D arrays is common, especially with high sampling rates and multi-channel systems. Transferring such large data using traditional API String methods would be inefficient due to:

- Increased memory overhead from plaintext encoding
- Performance issues with encoding/decoding large data
- Reduced readability in debug logs due to excessive text

## How It Works

MassData Support operates on a simple but effective principle:

1. **Encoding**: Converts large data into a reference string ("address") instead of directly encoding the data itself
2. **Transmission**: Sends this compact reference string through CSM's "invisible bus"
3. **Decoding**: The receiving CSM module uses the reference string to retrieve the original large data

The encoded result is a string containing three parts: "flag", "start position", and "size", which act as a "door number" to locate the actual data stored in a dedicated memory space.

## Key Benefits

1. **Efficient Transmission**: Transfers only a compact reference string instead of the entire data set, avoiding memory copies
2. **Memory Optimization**: Large data is stored in a single location regardless of the number of receivers
3. **Improved Readability**: Compact reference strings are easier to display in CSM Log controls without consuming excessive space

## Data Lifecycle

- MassData Support uses a circular buffer mechanism internally
- When the buffer is full, new data will overwrite old data from the beginning
- Once overwritten, the original data can no longer be recovered, and decoding will fail
- All CSM modules within the same application share the same MassData buffer space

## Best Practices

1. **Avoid Infinite Lifecycle Data**: Do not use MassData to store data that needs to persist indefinitely
2. **Configure Appropriate Cache Size**: Use the "Config MassData Parameter Cache Size.vi" to set an optimal buffer size
   - Not too large (to avoid wasting memory)
   - Not too small (to prevent frequent overwriting)
3. **Use Debugging Tools**: Utilize the provided debugging tools to monitor cache usage and determine the optimal configuration

## Installation

Install the addon via VIPM (VI Package Manager). After installation, you can find it in the CSM addons palette.

## Usage

1. Use encoding VIs to convert large data into MassData arguments
2. Transfer these arguments between CSM modules via CSM's parameter passing mechanism
3. Use decoding VIs on the receiving end to retrieve the original large data

## Examples

Check the example folder for demonstrations of:
1. MassData argument format
2. Displaying MassData cache status on the front panel
3. Using MassData in non-CSM frameworks
4. Integrating MassData with CSM

## Development Environment

LabVIEW 2017 or later

## License

This project is licensed under the MIT License - see the LICENSE file for details

