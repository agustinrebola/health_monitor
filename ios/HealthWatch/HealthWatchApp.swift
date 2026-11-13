import SwiftUI

@main
struct HealthWatchApp: App {
    @StateObject private var history = HistoryStore()
    @StateObject private var bluetooth = BluetoothManager()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(history)
                .environmentObject(bluetooth)
                .onAppear { bluetooth.history = history }
        }
        .onChange(of: scenePhase) { _, phase in
            // History writes are coalesced; flush before the app may be suspended.
            if phase == .background || phase == .inactive { history.saveNow() }
        }
    }
}
