from fastapi import Depends, FastAPI, HTTPException
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

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
