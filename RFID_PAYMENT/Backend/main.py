from fastapi import Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from database import SessionLocal
from datetime import datetime
from sqlalchemy.orm import Session
from database import engine, Base, depend_db
from schemas import Account
from CRUD import poll_transactions
from models import AccountModel
import CRUD, os, requests, asyncio, uvicorn 

Base.metadata.create_all(bind=engine)  

app = FastAPI()

# Cấu hình CORS để frontend có thể gọi API
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Cho phép tất cả origins (trong production nên chỉ định cụ thể)
    allow_credentials=True,
    allow_methods=["*"],  # Cho phép tất cả methods (GET, POST, PUT, DELETE, etc.)
    allow_headers=["*"],  # Cho phép tất cả headers
)

SEPAY_API_URL = os.getenv('SEPAY_API_URL')
API_TOKEN = os.getenv('API_TOKEN') 


@app.get("/transaction")
def get_transaction_detail():
    url = f"{SEPAY_API_URL}?since_id=28828682&limit=2"
    headers = {
        "Authorization": f"Bearer {API_TOKEN}",
        "Content-Type": "application/json"
    }
    response = requests.get(url, headers=headers)
    if response.status_code == 200:
        data = response.json()
        id = data["transactions"][0]["id"]
        return {"id": id}
    else:
        raise HTTPException(status_code=response.status_code, detail=response.text)

@app.on_event("startup")
async def startup_event():
    CRUD.polling_active = True
    db = SessionLocal()
    asyncio.create_task(poll_transactions(db))
    print("Started polling transactions every 2 seconds")

@app.on_event("shutdown")
async def shutdown_event():
    CRUD.polling_active = False
    print("Stopped polling transactions")


@app.post("/create_account")
def create_account(acc: Account, db: Session = Depends(depend_db)):
    return CRUD.create_account(db=db, id_card=acc.id_card, password=acc.password)

@app.delete("/delete_account")
def delete_account(id: str, db: Session = Depends(depend_db)):
    return CRUD.delete_account(db=db, id_student=id)

@app.get("/verify_card/{card_id}")
async def verify_card(card_id: str, db: Session = Depends(depend_db)):
    account = db.query(AccountModel).filter(
        AccountModel.id_card == card_id
    ).first()
    
    if account:
        return {"exists": True, "card_id": card_id}
    else:
        raise HTTPException(status_code=404, detail="Card not found")

@app.post("/verify_password")
async def verify_password(data: dict, db: Session = Depends(depend_db)):
    card_id = data.get("id_card")
    password = data.get("password")
    
    account = db.query(AccountModel).filter(
        AccountModel.id_card == card_id
    ).first()
    
    if not account:
        raise HTTPException(status_code=404, detail="Card not found")
    
    if account.password == password:
        return {"status": "success", "message": "Password correct"}
    else:
        raise HTTPException(status_code=401, detail="Wrong password")

@app.get("/get_balance/{card_id}")
async def get_balance(card_id: str, db: Session = Depends(depend_db)):
    account = db.query(AccountModel).filter(
        AccountModel.id_card == card_id
    ).first()
    
    if not account:
        raise HTTPException(status_code=404, detail="Card not found")
    
    return {"balance": account.so_du, "card_id": card_id}

@app.get("/get_balance_by_student/{id_student}")
async def get_balance_by_student(id_student: str, db: Session = Depends(depend_db)):
    """Tìm kiếm tài khoản theo MSSV và trả về thông tin số dư"""
    account = db.query(AccountModel).filter(
        AccountModel.id_student == id_student
    ).first()
    
    if not account:
        raise HTTPException(status_code=404, detail="Student ID not found")
    
    return {
        "id_student": account.id_student,
        "id_card": account.id_card,
        "balance": account.so_du
    }

@app.post("/process_payment")
async def process_payment(data: dict, db: Session = Depends(depend_db)):
    card_id = data.get("id_card")
    amount = data.get("amount")
    
    account = db.query(AccountModel).filter(
        AccountModel.id_card == card_id
    ).first()
    
    if not account:
        raise HTTPException(status_code=404, detail="Card not found")
    
    if account.so_du < amount:
        raise HTTPException(
            status_code=400, 
            detail="Insufficient balance"
        )
    
    # Trừ tiền
    account.so_du -= amount
    db.commit()
    
    return {
        "status": "success",
        "message": "Payment processed",
        "new_balance": account.so_du
    }

@app.get("/accounts/no_student_id")
async def get_accounts_without_student_id(db: Session = Depends(depend_db)):
    """Lấy danh sách tài khoản chưa có id_student (NULL hoặc empty)"""
    accounts = db.query(AccountModel).filter(
        (AccountModel.id_student == None) | (AccountModel.id_student == "")
    ).all()
    
    return {
        "total": len(accounts),
        "accounts": [
            {
                "id": acc.id,
                "id_card": acc.id_card,
                "so_du": acc.so_du,
                "created_at": acc.created_at.isoformat() if acc.created_at else None
            }
            for acc in accounts
        ]
    }

@app.post("/update_student_id")
async def update_student_id(data: dict, db: Session = Depends(depend_db)):
    """Cập nhật MSSV cho tài khoản sau khi xác thực mật khẩu"""
    id_card = data.get("id_card")
    password = data.get("password")
    id_student = data.get("id_student")
    
    if not all([id_card, password, id_student]):
        raise HTTPException(status_code=400, detail="Missing required fields")
    
    # Tìm tài khoản theo ID thẻ
    account = db.query(AccountModel).filter(
        AccountModel.id_card == id_card
    ).first()
    
    if not account:
        raise HTTPException(status_code=404, detail="Card not found")
    
    # Kiểm tra mật khẩu
    if account.password != password:
        raise HTTPException(status_code=401, detail="Wrong password")
    
    # Kiểm tra MSSV đã tồn tại chưa
    existing = db.query(AccountModel).filter(
        AccountModel.id_student == id_student,
        AccountModel.id != account.id
    ).first()
    
    if existing:
        raise HTTPException(status_code=409, detail="Student ID already exists")
    
    # Cập nhật MSSV
    account.id_student = id_student
    account.updated_at = datetime.now()
    db.commit()
    db.refresh(account)
    
    return {
        "status": "success",
        "message": "Student ID updated successfully",
        "id_card": id_card,
        "id_student": id_student
    }

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
