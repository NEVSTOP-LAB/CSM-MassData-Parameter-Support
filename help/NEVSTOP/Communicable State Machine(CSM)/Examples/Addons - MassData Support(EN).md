# CSM MassData Support Addon

## MassData Argument Format (1. MassData Argument Format.vi)

### Overview

MassData arguments are used within the CSM framework to transfer large amounts of data, such as arrays and waveform data. MassData arguments are also used for lossless transfer of complex data types. This example demonstrates the MassData argument formats supported by the CSM API.

### Introduction

This example simulates the generation of two sets of data: a set of I32 array data and a set of waveform data. Both sets are converted into MassData arguments.

CSM supports the following MassData argument format:

```
<massdata>Start:8057;Size:4004;DataType:1D I32
```

The meaning of each part is as follows:

- `<massdata>`: Indicates that this is a MassData argument.
- `Start`: The starting address of the MassData in memory.
- `Size`: The size of the MassData (in bytes).
- `DataType`: (Optional) The data type of the MassData. Depending on the conversion function selected, this part may not be included.

### Steps

- Step 1: I32 array data. The conversion function selected for this step is `CSM - Convert MassData to Argument With DataType.vim`, so it includes the `DataType` section.
  - Step 1.1: Construct raw data, which in a real-world program comes from hardware acquisition, data reception, etc.
  - Step 1.2: Call `CSM - Convert MassData to Argument With DataType.vim` to convert the raw data into a MassData argument. You can view the converted argument format on the front panel.
  - Step 1.3: Use `CSM - MassData Data Type String.vi` to retrieve the data type from the MassData argument string.
  - Step 1.4: Use `CSM - Argument Type.vi` to retrieve the CSM argument tag, which in this case is `<massdata>`.
  - Step 1.5: Convert the MassData argument back to raw data. You can view the converted data on the front panel. The converted data should match the original data exactly.
- Step 2: Waveform Data. The conversion function selected for this section is `CSM - Convert MassData to Argument.vim`, so it does not include the `DataType` section.
  - Step 2.1: Construct raw data, which in a real-world program comes from hardware acquisition, data reception, etc.
  - Step 2.2: Call `CSM - Convert MassData to Argument.vim` to convert the raw data into a MassData argument. You can view the converted argument format on the front panel. The `DataType` section is not included.
  - Step 2.3: Use `CSM - Argument Type.vi` to retrieve the CSM argument tag, which in this case is `<massdata>`.
  - Step 2.4: Convert the MassData argument back to raw data. You can view the converted data on the front panel. The converted data should match the original data exactly.



## View MassData Cache Status (2. Show MassData Cache Status in FP.vi)

### Overview

MassData uses a background circular queue to cache data. You can configure the size of the queue via the CSM API parameters. This example demonstrates how to view the MassData cache status.

### Introduction

MassData provides a helper function, `CSM - MassData Update Status Indicator.vi`, for viewing the status of the cache. This example illustrates how to use this function.

### Steps

- Step 1: Use `CSM - Config MassData Parameter Cache Size.vi` to set the cache size. You can visually verify on the interface that this setting takes effect.
- Step 2: In every loop iteration, construct a new set of data.
- Step 3: Use the CSM MassData API to pack and unpack the data, simulating usage in an actual program.
- Step 4: Use `CSM - MassData Update Status Indicator.vi` to update the vertical cache status indicator on the UI.
- Step 5: Use `CSM - MassData Update Status Indicator.vi` to update the horizontal cache status indicator on the UI.
- Step 6: Loop interval. Steps 3 through 4 are repeated in each iteration.
- Step 7: (Optional) You can use the CSM Tool provided by this addon to view the MassData cache status more quickly.



## Using MassData in a Non-CSM Framework (3. MassData in Non-CSM Framework.vi)

### Overview

You can use MassData in non-CSM framework. This example demonstrates how to utilize MassData in a non-CSM framework.

### Introduction

Using a Producer/Consumer design pattern example, this VI demonstrates how to use MassData in a non-CSM framework. The data producer is responsible for generating data, packing data into a MassData argument, and transmitting data to the data consumer via a queue. The data consumer is responsible for consuming the data, unpacking the MassData format back into raw data, and processing the raw data. This example illustrates the entire process.

### Steps

- Step 1: Use `CSM - Config MassData Parameter Cache Size.vi` to set the cache size.
- Step 2: Data producer loop
  - Step 2.1: Construct raw data, which in a real-world program comes from hardware acquisition, data reception, etc.
  - Step 2.2: Call `CSM - Convert MassData to Argument.vim` to convert the raw data into a MassData argument.
  - Step 2.3: Transmit the converted MassData argument to the data consumer via a queue.
- Step 3: Data consumer loop
  - Step 3.1: Receive the MassData argument from the queue.
  - Step 3.2: Call `CSM - Convert Argument to MassData.vim` to convert the MassData argument back into raw data.
  - Step 3.3: (Optional) Use `CSM - MassData Update Status Indicator.vi` to update the cache status indicator on the UI.



## Using MassData in the CSM Framework (4. MassData in CSM.vi)

### Overview

Demonstrates how to use MassData within the CSM framework.

### Introduction

This example uses a Producer/Consumer scenario to show how MassData operates within the CSM context.

A non-CSM loop acts as the data producer, responsible for generating data, packing the data into a MassData argument, and sending the data via a synchronous message to the CSM module, which acts as the data consumer. This example demonstrates this process.

### Steps

- Step 1: Use `CSM - Config MassData Parameter Cache Size.vi` to set the cache size.
- Step 2: Data producer loop. This loop is not a CSM module.
  - Step 2.1: Construct raw data, which in a real-world program comes from hardware acquisition, data reception, etc. Call `CSM - Convert MassData to Argument.vim` to convert the raw data into a MassData argument.
  - Step 2.2: Use `CSM - Wait and Send Message for Reply.vi` to send the data as an argument for `API: Update Waveform` to the CSM module, marking the sender as data producer.
  - Step 2.3: Data is sent only when the Generate button is pressed.
  - Step 2.4: (Optional) Use `CSM - MassData Update Status Indicator.vi` to update the cache status indicator on the UI.
- Step 3: Data consumer loop. This loop is a CSM module named "CSM".
  - Step 3.1: In the `API: Update Waveform` case, take the received argument and use `CSM - Convert Argument to MassData.vim` to convert the MassData argument back to raw data and display the data.
  - Step 3.3: (Optional) Other cases remain consistent with the template and are unmodified.
- Step 4: During the program exit process, use `CSM - Wait and Send Message for Reply.vi` to send the synchronous message "Macro: Exit" to the CSM module, causing the CSM module to shut down.