const API_URL = 'http://localhost:8000';
let accounts = [];
let selectedAccount = null;
let isLoading = false;
let loadCount = 0;

// Load accounts from API
async function loadAccounts() {
    // Prevent multiple simultaneous calls
    if (isLoading) {
        console.warn('⚠️ loadAccounts already in progress, skipping...');
        return;
    }
    
    isLoading = true;
    loadCount++;
    console.log(`📊 Loading accounts... (Call #${loadCount})`);
    
    try {
        const response = await fetch(`${API_URL}/accounts/no_student_id`);
        const data = await response.json();
        accounts = data.accounts;
        displayAccounts();
        console.log(`✓ Loaded ${accounts.length} accounts`);
    } catch (error) {
        console.error('❌ Error loading accounts:', error);
    } finally {
        isLoading = false;
    }
}

// Display accounts in table
function displayAccounts() {
    const loading = document.getElementById('loading');
    const table = document.getElementById('accountTable');
    const tbody = document.getElementById('tableBody');

    loading.classList.add('hidden');
    
    if (accounts.length === 0) {
        table.classList.add('hidden');
        return;
    }

    table.classList.remove('hidden');
    tbody.innerHTML = '';

    accounts.forEach((acc, index) => {
        const row = document.createElement('tr');
        const badge = acc.so_du > 0 ? 
            '<span class="badge badge-active">Có số dư</span>' : 
            '<span class="badge badge-empty">Chưa nạp</span>';
        
        row.innerHTML = `
            <td>${index + 1}</td>
            <td>${acc.id_card}</td>
            <td>${acc.so_du.toLocaleString('vi-VN')} đ</td>
            <td>${new Date(acc.created_at).toLocaleDateString('vi-VN')}</td>
            <td>${badge}</td>
        `;
        
        row.onclick = () => openModal(acc);
        tbody.appendChild(row);
    });
}

// Switch tabs
function switchTab(tabName) {
    // Update tab buttons
    const tabs = document.querySelectorAll('.tab');
    tabs.forEach(tab => tab.classList.remove('active'));
    event.target.classList.add('active');
    
    // Update tab content
    const tabPanes = document.querySelectorAll('.tab-pane');
    tabPanes.forEach(pane => pane.classList.remove('active'));
    
    if (tabName === 'update') {
        document.getElementById('updateTab').classList.add('active');
    } else if (tabName === 'balance') {
        document.getElementById('balanceTab').classList.add('active');
    }
    
    // Clear search input and results when switching tabs
    const searchInput = document.getElementById('searchMSSV');
    const resultDiv = document.getElementById('balanceResult');
    const errorDiv = document.getElementById('balanceError');
    
    if (searchInput) searchInput.value = '';
    if (resultDiv) resultDiv.classList.add('hidden');
    if (errorDiv) errorDiv.classList.add('hidden');
}

// Search balance by MSSV
async function searchBalance() {
    const mssv = document.getElementById('searchMSSV').value.trim();
    const resultDiv = document.getElementById('balanceResult');
    const errorDiv = document.getElementById('balanceError');
    
    // Hide previous results
    resultDiv.classList.add('hidden');
    errorDiv.classList.add('hidden');
    
    if (!mssv) {
        errorDiv.textContent = '⚠️ Vui lòng nhập MSSV';
        errorDiv.classList.remove('hidden');
        return;
    }
    
    try {
        const response = await fetch(`${API_URL}/get_balance_by_student/${mssv}`);
        
        if (!response.ok) {
            throw new Error('Không tìm thấy tài khoản');
        }
        
        const data = await response.json();
        
        // Display results
        document.getElementById('resultMSSV').textContent = data.id_student;
        document.getElementById('resultCardId').textContent = data.id_card;
        document.getElementById('resultBalance').textContent = 
            data.balance.toLocaleString('vi-VN') + ' đ';
        
        resultDiv.classList.remove('hidden');
        
    } catch (error) {
        errorDiv.textContent = '❌ ' + error.message;
        errorDiv.classList.remove('hidden');
    }
}

// Open modal
function openModal(account) {
    selectedAccount = account;
    const modal = document.getElementById('modal');
    const cardId = document.getElementById('cardId');
    const studentId = document.getElementById('studentId');
    const password = document.getElementById('password');
    const alert = document.getElementById('alert');

    cardId.value = account.id_card;
    studentId.value = '';
    password.value = '';
    alert.classList.add('hidden');
    
    modal.style.display = 'block';
}

// Close modal
function closeModal() {
    document.getElementById('modal').style.display = 'none';
}

// Update student ID
async function updateStudentId(e) {
    e.preventDefault();
    
    const studentId = document.getElementById('studentId').value;
    const password = document.getElementById('password').value;
    const alert = document.getElementById('alert');

    try {
        const response = await fetch(`${API_URL}/update_student_id`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                id_card: selectedAccount.id_card,
                id_student: studentId,
                password: password
            })
        });

        const data = await response.json();

        if (!response.ok) {
            throw new Error(data.detail || 'Cập nhật thất bại');
        }

        alert.className = 'alert alert-success';
        alert.textContent = '✓ Cập nhật thành công!';
        alert.classList.remove('hidden');

        setTimeout(() => {
            closeModal();
            loadAccounts();
        }, 1500);

    } catch (error) {
        alert.className = 'alert alert-error';
        
        if (error.message.includes('401')) {
            alert.textContent = '❌ Mật khẩu không đúng!';
        } else if (error.message.includes('409')) {
            alert.textContent = '❌ MSSV đã tồn tại!';
        } else {
            alert.textContent = '❌ ' + error.message;
        }
        
        alert.classList.remove('hidden');
    }
}

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    console.log('🚀 Page loaded, initializing...');
    loadAccounts();
    
    document.querySelector('.close').onclick = closeModal;
    document.getElementById('cancelBtn').onclick = closeModal;
    document.getElementById('updateForm').onsubmit = updateStudentId;
    
    window.onclick = (e) => {
        const modal = document.getElementById('modal');
        if (e.target === modal) closeModal();
    };
});

// Detect page visibility changes
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
        console.log('👁️ Page became visible');
    }
});
