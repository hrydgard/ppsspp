# How to Update the PFX Key for UWP Signing

This guide explains how to generate and update the PFX (Personal Information Exchange) certificate used to sign the PPSSPP UWP app.

---

## Prerequisites

- **Visual Studio** installed with UWP development workload
- Access to the project solution file

---

## Step 1: Open the Package Manifest

1. In Visual Studio, open the PPSSPP UWP solution
2. In **Solution Explorer**, navigate to the `UWP` project
3. Double-click on `Package.appxmanifest` or `PackageGold.appxmanifest` to open the manifest viewer

---

## Step 2: Go to the Packaging Tab

1. Click on the **Packaging** tab in the manifest viewer
2. You'll see the current certificate information displayed here

---

## Step 3: Choose a Certificate

1. Click the **Choose Certificate...** button
2. A dialog will appear with several options:
   - **Create...** - Create a new self-signed test certificate
   - **Select from Store...** - Choose an existing certificate from Windows Certificate Store
   - **Select from File...** - Browse for an existing .pfx file

---

## Step 4a: Create a New Test Certificate

If creating a new self-signed certificate:

1. Click **Create...**
2. Enter a **Publisher Common Name** (e.g., `CN=YourName`)
3. Optionally set a password
4. Click **OK**

---

## Step 4b: Select an Existing Certificate

If using an existing .pfx file:

1. Click **Select from File...**
2. Go to your `.pfx` file location
3. Enter the certificate password if prompted
4. Click **OK**

---

<!-- 
	TODO: Figure out how to update the PFX key without going through Visual Studio (Maybe powershell?)
-->
