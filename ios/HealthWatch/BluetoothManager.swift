import CoreBluetooth
import Foundation
import Combine

@MainActor
final class BluetoothManager: NSObject, ObservableObject {
    @Published private(set) var devices: [CBPeripheral] = []
    @Published private(set) var snapshot: WatchSnapshot?
    @Published private(set) var status = "Bluetooth starting"
    @Published private(set) var connected = false
    @Published private(set) var invalidPackets = 0
    var history: HistoryStore?

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }
    func scan() {
        guard central.state == .poweredOn else { status = "Bluetooth unavailable"; return }
        devices = []
        status = "Scanning"
        central.scanForPeripherals(withServices: [CBUUID(nsuuid: WatchProtocol.service)], options: nil)
    }
    func connect(_ device: CBPeripheral) {
        central.stopScan()
        peripheral = device
        status = "Connecting"
        central.connect(device, options: nil)
    }
    func disconnect() {
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
    }
}

extension BluetoothManager: CBCentralManagerDelegate, CBPeripheralDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        status = central.state == .poweredOn ? "Ready to scan" : "Bluetooth unavailable or permission denied"
    }
    func centralManager(_ central: CBCentralManager, didDiscover device: CBPeripheral,
                        advertisementData: [String : Any], rssi RSSI: NSNumber) {
        if !devices.contains(where: { $0.identifier == device.identifier }) { devices.append(device) }
    }
    func centralManager(_ central: CBCentralManager, didConnect device: CBPeripheral) {
        connected = true
        status = "Discovering service"
        device.delegate = self
        device.discoverServices([CBUUID(nsuuid: WatchProtocol.service)])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect device: CBPeripheral, error: Error?) {
        connected = false
        status = error?.localizedDescription ?? "Connection failed"
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral device: CBPeripheral, error: Error?) {
        connected = false
        snapshot = nil
        status = error?.localizedDescription ?? "Disconnected"
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error { status = error.localizedDescription; return }
        for service in peripheral.services ?? [] where service.uuid == CBUUID(nsuuid: WatchProtocol.service) {
            peripheral.discoverCharacteristics([CBUUID(nsuuid: WatchProtocol.snapshot)], for: service)
        }
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error { status = error.localizedDescription; return }
        for characteristic in service.characteristics ?? [] where characteristic.uuid == CBUUID(nsuuid: WatchProtocol.snapshot) {
            peripheral.setNotifyValue(true, for: characteristic)
            peripheral.readValue(for: characteristic)
            status = "Connected"
        }
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil, let data = characteristic.value else { invalidPackets += 1; return }
        guard let value = WatchSnapshot.decode(data, deviceID: peripheral.identifier.uuidString) else {
            invalidPackets += 1; return
        }
        snapshot = value
        history?.add(value)
    }
}
