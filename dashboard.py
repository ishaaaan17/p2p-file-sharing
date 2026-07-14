import socket
import threading
import json
import time
import streamlit as st
import pandas as pd

# --- STATE MANAGEMENT CONFIGURATION ---
# Streamlit updates on every rerun, so we store persistent peer data in session_state
if "swarm_data" not in st.session_state:
    st.session_state.swarm_data = {
        "local_peer_id": "1001",
        "local_port": "7777",
        "pieces_status": ["Missing"] * 6, # Track 6 pieces: Missing, Downloading, Completed
        "connected_peers": {} # Stores metrics: {port: {downloaded: 0, uploaded: 0, status: "UNCHOKED"}}
    }

# --- BACKGROUND UDP RECEIVER THREAD ---
def network_listener_loop():
    """Listens for local UDP monitoring frames sent from the C++ core engine."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("127.0.0.1", 6000)) # Dashboard listens on local port 6000
    except Exception as e:
        return # Already bound or port occupied
        
    while True:
        try:
            data, _ = sock.recvfrom(2048)
            # Expected JSON format: {"type": "PEER_UPDATE", "port": 9999, "downloaded": 1000, "uploaded": 0, "status": "UNCHOKED"}
            # Expected JSON format: {"type": "PIECE_UPDATE", "index": 0, "status": "Completed"}
            frame = json.loads(data.decode('utf-8'))
            
            if frame["type"] == "PEER_UPDATE":
                p_port = str(frame["port"])
                st.session_state.swarm_data["connected_peers"][p_port] = {
                    "Downloaded (Bytes)": frame["downloaded"],
                    "Uploaded (Bytes)": frame["uploaded"],
                    "Game-Theory State": frame["status"]
                }
            elif frame["type"] == "PIECE_UPDATE":
                idx = frame["index"]
                if 0 <= idx < len(st.session_state.swarm_data["pieces_status"]):
                    st.session_state.swarm_data["pieces_status"][idx] = frame["status"]
        except Exception:
            pass
        time.sleep(0.05)

# Start the background socket thread once
if "thread_started" not in st.session_state:
    listener_thread = threading.Thread(target=network_listener_loop, daemon=True)
    listener_thread.start()
    st.session_state.thread_started = True

# --- STREAMLIT FRONTEND RENDERING ---
st.set_page_config(page_title="P2P Swarm Dashboard", page_icon="⚡", layout="wide")

st.title("⚡ Full Swarm Production Distribution Dashboard")
st.markdown("Real-time telemetry and game-theoretic visualization engine connected to the native C++ P2P core.")
st.markdown("---")

# Layout Column Structure
col1, col2 = st.columns([1, 2])

with col1:
    st.header("🔑 Local Node Config")
    st.metric(label="Local Peer ID", value=st.session_state.swarm_data["local_peer_id"])
    st.metric(label="Local Listening Port", value=st.session_state.swarm_data["local_port"])
    
    st.subheader("🛠️ Quick Swarm Actions")
    if st.button("Broadcast Swarm Handshake (SYN)", use_container_width=True):
        st.info("Triggering background mesh discovery across target terminal grids...")
    if st.button("Graceful Disconnection (FIN)", use_container_width=True):
        st.warning("Sending teardown indicators out to active neighbors...")

with col2:
    st.header("📦 Distributed Bitfield Progress Map")
    
    # Render custom visual blocks for the file pieces
    status_emoji_map = {"Missing": "⬛ Empty", "Downloading": "⏳ Fetching", "Completed": "🟩 Active"}
    cols = st.columns(6)
    for i in range(6):
        status = st.session_state.swarm_data["pieces_status"][i]
        with cols[i]:
            st.info(f"**Piece #{i}**\n\n{status_emoji_map.get(status, status)}")

    # Calculate download completion percentage
    completed_count = st.session_state.swarm_data["pieces_status"].count("Completed")
    progress_percentage = int((completed_count / 6) * 100)
    st.progress(completed_count / 6)
    st.caption(f"Swarm Sync Progress: **{progress_percentage}%** ({completed_count}/6 blocks committed to disk)")

    st.markdown("---")
    st.header("🎛️ Connected Active Peer Node Matrix")
    
    if st.session_state.swarm_data["connected_peers"]:
        df = pd.DataFrame.from_dict(st.session_state.swarm_data["connected_peers"], orient='index')
        st.dataframe(df, use_container_width=True)
    else:
        st.info("Awaiting incoming peer sessions... Start your C++ nodes to establish dynamic tracking loops.")

# Auto-refresh helper to simulate real-time telemetry updates
time.sleep(1)
st.rerun()